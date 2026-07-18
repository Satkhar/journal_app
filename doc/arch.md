# Architecture

## Слои

```mermaid
flowchart LR
  UI[MainWindow / Qt Widgets] --> APP[JournalApp]
  APP --> ST[IJournalStorage]
  ST --> LOCAL[JournalLocal]
  ST --> REMOTE[JournalRemote]
  LOCAL --> SQLITE[SqliteConnect]
  SQLITE --> LDB[(SQLite schema v7)]
  REMOTE --> API[libsql HTTP API]
  API --> RDB[(Remote schema v7)]
  UI --> SYNC[SyncService]
  SYNC --> ST
  UI --> EAPP[EventApp]
  EAPP --> EST[IEventStorage]
  EST --> ESQL[EventSqliteStorage]
  ESQL --> EDB[(events_data.db, schema v1)]
```

`MainWindow` отвечает за widgets и запуск use cases. `JournalApp` работает
через `IJournalStorage`. SQL находится в `SqliteConnect`/`JournalRemote`.
`SyncService` переносит полный snapshot одного месяца.

## Доменная модель

### `Participant`

Глобальный человек:

- `ParticipantId` — стабильный UUID;
- `displayName` — редактируемое историчное имя для журнала.

Имя не является ключом. Одинаковые имена допустимы.

### `ParticipantProfile`

Отдельная глобальная карточка: историчное имя, optional ФИО, один opaque contact
(VK, Telegram или телефон), nullable birthday, типизированное звание,
plain-text notes и `archived_at`. Birthday хранится как day/month и optional
year; фиктивный год не используется. Archive не удаляет membership или
attendance. Начальный порядок званий: паж, оруженосец, новичок, рекрут, гость,
рыцарь. UI использует этот порядок для сортировки и визуальной группировки.

### `month_participants`

Связь участника с конкретным `(year, month)`. Удаление этой связи убирает
человека из месяца, но не удаляет `Participant` и историю других месяцев.

### `attendance`

Отметка принадлежит `(year, month, day, participant_id)`. Составной FK
запрещает attendance без membership этого месяца.

### `participant_day_markers`

Семантическая отметка `(year, month, day, participant_id)`: bitmask событий
`Payment`, `SpecialTraining`, `FirstVisit`, `Other`, `LedTraining` и plain-text
note до 500 символов. `LedTraining` означает, что этот участник вёл тренировку
именно в эту дату; это не свойство профиля. Цвет и значок — только
UI-представление.
Составной FK на membership удаляет отметки вместе с участником месяца. FK на
`month_days` намеренно нет: отметка выключенной даты сохраняется.

### `EventRecord`

Турниры — отдельный bounded context и отдельная локальная SQLite-БД. Для
новой установки обе БД лежат в `QStandardPaths::AppLocalDataLocation`; если
обнаружен старый `test_data.db` в рабочем каталоге, `events_data.db` создаётся
рядом с ним. Неявного перемещения существующего журнала нет. Запись содержит
UUID, название, дату, общую plain-text заметку, список внутренних участников и
строки боёв. Сторона боя — либо UUID внутреннего участника, либо свободное имя
внешнего человека; счёт хранится двумя целыми числами.

Физический FK между двумя SQLite-файлами невозможен. Поэтому БД турниров
хранит UUID внутреннего участника и snapshots историчного имени и ФИО. UUID
сохраняет логическую связь, snapshots не дают старому турниру потерять
контекст после переименования или отсутствия основной БД. Aggregate читается в
одной SQLite read transaction; `revision` и compare-and-swap блокируют тихую
перезапись более новой версии другим процессом. Турниры пока не входят в
remote sync.

### `MonthSnapshot`

Содержит:

- явное состояние `Missing`, `Ready` или `Error` и текст ошибки;
- participants текущего месяца;
- active days;
- attendance.
- day markers.

Snapshot применяется целиком только в sync-сценариях. Обычное сохранение UI
делает upsert attendance и не меняет membership/profile.

## SQLite schema v7

- `participants`: `birth_day`, `birth_month`, `birth_year`, `rank`, `full_name`,
  `contact`, `notes`, `archived_at`;
- `month_participants`;
- `attendance`;
- `month_days`;
- `participant_day_markers`.

Обязательны:

- `PRAGMA foreign_keys = ON` для каждого connection;
- `CHECK` для year/month/day/boolean;
- unique primary keys;
- индексы чтения месяца и истории участника;
- `PRAGMA user_version = 7`.

Schema v2 мигрирует последовательно v2 -> v3 -> v4 -> v5 -> v7. Schema v3-v5
также мигрируют последовательно и транзакционно. Для существующих участников
миграция v4 -> v5 назначает `rank = 'guest'`; v5 -> v7 назначает пустые ФИО и
контакт и расширяет допустимую marker mask с 15 до 31. Unversioned legacy schema
сначала мигрирует в v3, затем до v7:
`dd.MM.yyyy` сохраняет год, а `dd.MM` привязывается к единственному году
настройки `month_days`.
Неоднозначное соответствие месяца нескольким годам отклоняется. Profile
triggers проверяют имя, ФИО, контакт, notes, звание и календарную корректность
birthday.

Промежуточная development-схема с profile-column `is_trainer` также называлась
v6, но имеет другой контракт. При открытии ограничение маркеров обновляется
без потери данных, значения старого флага архивируются, устаревшая колонка
удаляется, версия становится v7.

`saveActiveDays()` не удаляет attendance и day markers выключенных дней. При
повторном включении сохраненные данные восстанавливаются.

## Month setup contract

Чтение месяца не создаёт данные. `IJournalStorage::getMonthState()` отделяет
отсутствующий месяц от пустого настроенного месяца и от ошибки storage. Для
нового local-месяца UI предлагает создать его с нуля или атомарно перенести
участников и дни из другого месяца. При ошибке чтения редактирование блокируется,
чтобы пустой UI не перезаписал существующие данные.

## Sync contract

Push/pull работают с `MonthSnapshot`, а не с `name + day`.

- Push заменяет remote membership/active days/attendance/day markers месяца.
- Pull заменяет local membership/active days/attendance/day markers месяца.
- Global participant rows upsert-ятся, но не удаляются full replace.
- Ошибка remote read не трактуется как пустой месяц.
- Remote writes используют Hrana batch: DML идет по `ok`-conditions, rollback —
  по отрицанию успешного commit.
- Pull читает participants/days/attendance/day markers одной read transaction.
- Profile fields не входят в `MonthSnapshot` и не синхронизируются до этапа 4.

Текущий HTTP-клиент синхронный и использует вложенный `QEventLoop`. Это PoC,
не production transport. До синхронизации фото/персональных данных нужны async,
auth, TLS, revisions и conflict handling.
