# Detailed Code Walkthrough

Этот документ описывает текущее состояние кода максимально подробно:
- какие классы за что отвечают;
- как проходит путь данных от UI до хранилищ;
- как работают кнопки;
- почему приняты текущие решения (read-only remote, push/pull через сервис).

## 1. Архитектура на уровне слоев

### 1. UI слой (`MainWindow`)
- Файл: `App/Src/mainwindow.cpp`, `App/Inc/mainwindow.hpp`
- Отвечает за:
  - отрисовку таблицы и элементов управления;
  - обработку кликов пользователя;
  - вызов use-case и sync сервиса;
  - отображение статусов в `statusbar`.

UI не должен знать детали SQL/HTTP протоколов. Он работает с:
- `JournalApp` (текущий активный storage),
- `SyncService` (операции push/pull).

### 2. Use-case слой (`JournalApp`)
- Файл: `App/Src/JournalApp.cpp`, `App/Inc/JournalApp.hpp`
- Отвечает за:
  - загрузку месяца (`loadMonth`);
  - добавление/удаление пользователя;
  - сохранение месяца.

Ключевой флаг:
- `allowBootstrapWrites`:
  - `true` для local режима: разрешает автоинициализацию пустого месяца (Alice);
  - `false` для remote режима: запрещает неявные записи при чтении.

### 3. Абстракция storage (`IJournalStorage`)
- Файл: `App/Inc/IJournalStorage.hpp`
- Единый интерфейс для local и remote:
  - `getUsersForMonth`
  - `getMonth`
  - `saveMonth`
  - `addUser`
  - `deleteUser`

### 4. Local storage
- `JournalLocal`: `App/Src/JournalLocal.cpp`
- `SqliteConnect`: `App/Src/SqliteConnect.cpp`

`JournalLocal` — тонкий адаптер `IJournalStorage` к `SqliteConnect`.
`SqliteConnect` содержит SQL-логику SQLite:
- создание схемы;
- чтение и запись месяца;
- транзакции.

### 5. Remote storage
- `JournalRemote`: `App/Src/JournalRemote.cpp`

Реализует `IJournalStorage` поверх HTTP API `libsql/sqld`:
- endpoint: `POST /v2/pipeline`;
- команды SQL передаются в JSON-массиве `requests`;
- ответы парсятся из `results`.

### 6. Sync слой
- `SyncService`: `App/Src/SyncService.cpp`

Содержит операции синхронизации:
- `pushMonthToServer`: local -> remote;
- `pullMonthToLocal`: remote -> local.

Важно: UI не должен напрямую писать HTTP-логику. Это вынесено сюда.

## 2. Основной UI сценарий

### Инициализация окна
1. `MainWindow` вызывает:
   - `createEmptyTable()`
   - `createCheckTable()`
   - `setupStorageControls()`
2. Из env читаются:
   - `JOURNAL_STORAGE_MODE`
   - `JOURNAL_SERVER_URL`
3. Через `setupStorage(...)` поднимается стартовый storage.
4. Выставляются:
   - `activeStorageMode_`
   - `activeServerUrl_`
   - бейдж режима (`updateModeBadge()`)
   - доступность кнопок (`updateEditControlsByMode()`).

### Режимы работы
- `LOCAL`:
  - редактирование разрешено;
  - работают `Add/Del/Save/Push/Pull`.
- `REMOTE (read-only)`:
  - редактирование отключено;
  - только просмотр данных удаленной БД.

## 3. Логика кнопок

### `Local`
- Подключает локальное хранилище (`SqliteConnect -> JournalLocal`).
- Создает `JournalApp(..., true)`.
- Обновляет бейдж и перерисовывает месяц.

### `Remote`
- Подключает удаленное хранилище по URL (`JournalRemote`).
- Создает `JournalApp(..., false)`, чтобы чтение не писало на сервер.
- Обновляет бейдж и перерисовывает месяц.

### `Read Base`
- Всегда читает локальную SQLite БД.
- Не переключает active storage.
- Используется как "показать local состояние сейчас".

### `Save Current Table`
- Сохраняет таблицу в active `JournalApp`.
- В remote режиме кнопка выключена.

### `Push to Server`
- Доступна только в local режиме.
- Собирает текущие данные таблицы (`collectMonthFromTable`).
- Вызывает `SyncService::pushMonthToServer(...)`.

### `Pull from Server`
- Доступна только в local режиме.
- Вызывает `SyncService::pullMonthToLocal(...)`.
- После успеха делает `refreshMonth()`.

## 4. Важные детали реализации

### 1) Уникальные Qt SQL connection names
- В `SqliteConnect::open()` connection name генерируется через `QUuid`.
- Причина:
  - раньше фиксированное имя приводило к конфликтам;
  - временные подключения (`Read/Pull`) могли удалить активное подключение окна.

### 2) Защита от затирания local базы при pull
- В `JournalRemote` ведется `lastError_`.
- В `SyncService::pullMonthToLocal`:
  - если `getMonth` вернул ошибку (`lastError` не пуст), локальная БД не переписывается.

### 3) Remote read-only как продуктовый режим
- `JournalApp` создается с `allowBootstrapWrites=false` для remote.
- Это блокирует неявные автозаписи при `loadMonth()`.

## 5. Формат данных

Сущность:
- `AttendanceRecord { userName, day, isChecked }`

Таблица `users`:
- `id INTEGER PRIMARY KEY AUTOINCREMENT`
- `name TEXT`
- `date TEXT`
- `is_checked INTEGER`

Для текущего PoC используется стратегия:
- хранить записи по дням;
- операции `saveMonth` делают полную перезапись месяца.

## 6. Ограничения текущего состояния

1. Нет ревизий месяца (`month_revision`/`updated_at`) для conflict detection.
2. Нет дельта-синхронизации (используется полная перезапись).
3. Pull/Push ручные (по кнопкам), без автоматического расписания.
4. Для прод-сценария нужны auth/TLS и политика merge конфликтов.

## 7. Что читать в коде сначала

Рекомендуемый порядок чтения:
1. `App/Inc/IJournalStorage.hpp`
2. `App/Inc/JournalApp.hpp`
3. `App/Src/JournalApp.cpp`
4. `App/Src/mainwindow.cpp`
5. `App/Src/SyncService.cpp`
6. `App/Src/JournalRemote.cpp`
7. `App/Src/SqliteConnect.cpp`

Так проще понять путь данных:
UI -> use-case -> storage -> (local SQL | remote HTTP).

