# Architecture

## Слои

```mermaid
flowchart LR
  UI[MainWindow / Qt Widgets] --> APP[JournalApp]
  APP --> ST[IJournalStorage]
  ST --> LOCAL[JournalLocal]
  ST --> REMOTE[JournalRemote]
  LOCAL --> SQLITE[SqliteConnect]
  SQLITE --> LDB[(SQLite schema v4)]
  REMOTE --> API[libsql HTTP API]
  API --> RDB[(Remote schema v4)]
  UI --> SYNC[SyncService]
  SYNC --> ST
```

`MainWindow` отвечает за widgets и запуск use cases. `JournalApp` работает
через `IJournalStorage`. SQL находится в `SqliteConnect`/`JournalRemote`.
`SyncService` переносит полный snapshot одного месяца.

## Доменная модель

### `Participant`

Глобальный человек:

- `ParticipantId` — стабильный UUID;
- `displayName` — редактируемое отображаемое имя.

Имя не является ключом. Одинаковые имена допустимы.

### `ParticipantProfile`

Отдельная глобальная карточка: имя, nullable birthday, plain-text notes и
`archived_at`. Birthday хранится как day/month и optional year; фиктивный год
не используется. Archive не удаляет membership или attendance.

### `month_participants`

Связь участника с конкретным `(year, month)`. Удаление этой связи убирает
человека из месяца, но не удаляет `Participant` и историю других месяцев.

### `attendance`

Отметка принадлежит `(year, month, day, participant_id)`. Составной FK
запрещает attendance без membership этого месяца.

### `participant_day_markers`

Семантическая отметка `(year, month, day, participant_id)`: bitmask событий
`Payment`, `SpecialTraining`, `FirstVisit`, `Other` и plain-text note до 500
символов. Цвет и значок — только UI-представление. Составной FK на membership
удаляет отметки вместе с участником месяца. FK на `month_days` намеренно нет:
отметка выключенной даты сохраняется.

### `MonthSnapshot`

Содержит:

- явное состояние `Missing`, `Ready` или `Error` и текст ошибки;
- participants текущего месяца;
- active days;
- attendance.
- day markers.

Snapshot применяется целиком только в sync-сценариях. Обычное сохранение UI
делает upsert attendance и не меняет membership/profile.

## SQLite schema v4

- `participants`: `birth_day`, `birth_month`, `birth_year`, `notes`,
  `archived_at`;
- `month_participants`;
- `attendance`;
- `month_days`;
- `participant_day_markers`.

Обязательны:

- `PRAGMA foreign_keys = ON` для каждого connection;
- `CHECK` для year/month/day/boolean;
- unique primary keys;
- индексы чтения месяца и истории участника;
- `PRAGMA user_version = 4`.

Schema v2 мигрирует последовательно v2 -> v3 -> v4. Schema v3 мигрирует в v4
транзакционно. Unversioned legacy schema сначала мигрирует в v3, затем в v4:
`dd.MM.yyyy` сохраняет год, а `dd.MM` привязывается к единственному году
настройки `month_days`.
Неоднозначное соответствие месяца нескольким годам отклоняется. Profile
triggers проверяют имя, notes и календарную корректность birthday.

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
