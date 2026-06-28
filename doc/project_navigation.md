# Project Navigation

Этот документ фиксирует текущую карту проекта для быстрой навигации перед
следующими задачами. Это не план развития, а снимок того, что где лежит и какие
файлы обычно нужно открывать для разных типов изменений.

## Кратко о проекте

`journal_app` - desktop-приложение на C++17 / Qt6 Widgets для ведения месячного
журнала посещаемости. Основная таблица показывает пользователей строками и дни
месяца колонками. Данные можно хранить локально в SQLite или читать/писать через
remote storage на базе libsql/sqld HTTP API.

Сборка описана в `CMakeLists.txt`. Проект ожидает GNU/GCC toolchain и Qt6
компоненты `Core`, `Gui`, `Widgets`, `Sql`, `Network`.

## Текущая структура

```text
journal_app/
  CMakeLists.txt              # цель journal_app, список исходников, Qt6, C++17
  README.md                   # пользовательское описание, сборка, базовое usage
  LICENSE
  journal_app.ui              # Qt Designer UI: кнопки, календарь, layout'ы

  App/
    Inc/                      # публичные заголовки проекта
      config.h                # DB_PATH, режим/URL сервера по умолчанию, timeout
      journal_app.h           # сгенерированный/зафиксированный UI header
      mainwindow.hpp          # главный Qt UI-контроллер
      JournalApp.hpp          # use-case слой приложения
      IJournalStorage.hpp     # общий контракт локального и remote storage
      JournalLocal.hpp        # адаптер IJournalStorage -> SqliteConnect
      JournalRemote.hpp       # remote storage через libsql HTTP pipeline
      SyncService.hpp         # push/pull месяца между local и remote
      SqliteConnect.hpp       # низкоуровневый SQLite storage
      dbManager.hpp           # устаревший DB manager
      mainTableManager.hpp    # устаревший manager UI-таблицы
      checkTableManager.hpp   # устаревшая заготовка manager чекбокс-таблицы

    Src/
      main.cpp                # QApplication, MainWindow, Windows UTF-8 console
      mainwindow.cpp          # UI wiring, таблица, режимы local/remote, sync buttons
      JournalApp.cpp          # сценарии load/add/delete/save для выбранного месяца
      JournalLocal.cpp        # тонкий adapter к SqliteConnect
      JournalRemote.cpp       # SQL-over-HTTP к libsql/sqld /v2/pipeline
      SyncService.cpp         # push local->server и pull server->local
      SqliteConnect.cpp       # SQLite schema и CRUD месяца
      dbManager.cpp           # устаревший код старого доступа к БД
      mainTableManager.cpp    # устаревший код старого управления таблицей
      checkTableManager.cpp   # устаревшая почти пустая реализация

  doc/
    arch.md                   # архитектурный план MVP и server stage
    arch.mmd                  # диаграмма архитектуры
    build_setup.md            # Windows/MSYS2/GCC/Qt6 сборка
    docker_setup.md           # Docker setup для libsql/sqld
    server_sync_plan.md       # PoC-план синхронизации local/remote
    project_navigation.md     # этот навигационный снимок
```

## Главный поток выполнения

1. `App/Src/main.cpp` создает `QApplication`, `MainWindow` и запускает event loop.
2. `MainWindow` в `App/Src/mainwindow.cpp`:
   - загружает UI из `journal_app.ui` через `Ui::MainWindow`;
   - создает таблицы `bigTable` и скрытую `checkTable`;
   - создает controls для выбора `Local` / `Remote`;
   - инициализирует `JournalApp` с нужной реализацией `IJournalStorage`;
   - связывает кнопки с add/delete/read/save/push/pull действиями.
3. `JournalApp` в `App/Src/JournalApp.cpp` хранит выбранный месяц и вызывает
   storage-методы. При пустом local месяце может создать стартового пользователя
   `Alice`; для remote режима bootstrap-запись отключена.
4. `IJournalStorage` задает общий контракт:
   - `getUsersForMonth`
   - `getMonth`
   - `saveMonth`
   - `addUser`
   - `deleteUser`
5. `JournalLocal` делегирует все операции в `SqliteConnect`.
6. `JournalRemote` исполняет SQL на сервере через POST `<baseUrl>/v2/pipeline`.
7. `SyncService` создает временный `JournalRemote` и делает:
   - `pushMonthToServer`: текущий local snapshot -> remote;
   - `pullMonthToLocal`: remote snapshot -> переданный local storage.

## Слои и ответственность

### UI слой

Основной файл: `App/Src/mainwindow.cpp`.

Здесь находятся:
- создание и перерисовка таблицы месяца (`createEmptyTable`, `renderMonth`);
- чтение чекбоксов из таблицы (`collectMonthFromTable`);
- обработчики кнопок `Add`, `Delete`, `Read Base`, `Save Current Table`,
  push/pull серверной синхронизации;
- переключение режимов storage (`setupStorage`, `connectLocalFromUi`,
  `connectRemoteFromUi`);
- статусная строка и badge текущего режима.

При изменениях в UX, кнопках, календаре, таблице или статусах обычно начинать с:
- `journal_app.ui`
- `App/Inc/mainwindow.hpp`
- `App/Src/mainwindow.cpp`

### Use-case слой

Основные файлы:
- `App/Inc/JournalApp.hpp`
- `App/Src/JournalApp.cpp`

Здесь должны жить сценарии приложения, не привязанные к конкретному UI-виджету:
загрузить месяц, добавить пользователя, удалить пользователя, сохранить месяц.

Если нужно менять бизнес-поведение, которое одинаково для local/remote, начинать
с `JournalApp`.

### Storage contract и модели

Основной файл: `App/Inc/IJournalStorage.hpp`.

Здесь определены:
- `AttendanceRecord`: `userName`, `day`, `isChecked`;
- интерфейс `IJournalStorage`.

Если меняется доменная форма данных, дата, поля записи или контракт хранения,
начинать с этого файла, затем пройти реализации `JournalLocal`, `JournalRemote`,
`SqliteConnect` и места сборки таблицы в `MainWindow`.

### Local SQLite storage

Основные файлы:
- `App/Inc/SqliteConnect.hpp`
- `App/Src/SqliteConnect.cpp`
- `App/Inc/JournalLocal.hpp`
- `App/Src/JournalLocal.cpp`

Текущая SQLite-схема создается автоматически:

```sql
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  date TEXT NOT NULL,
  is_checked INTEGER NOT NULL
)
```

Локальная дата сейчас пишется в старом формате `dd.MM`, а чтение месяца ищет
`__.MM%`, чтобы сохранить совместимость со старой БД. Путь к БД задан в
`App/Inc/config.h` как `DB_PATH "test_data.db"`.

Для задач про локальную БД, SQL, формат хранения и производительность сохранения
месяца начинать с `SqliteConnect.cpp`.

### Remote storage

Основные файлы:
- `App/Inc/JournalRemote.hpp`
- `App/Src/JournalRemote.cpp`
- `doc/docker_setup.md`
- `doc/server_sync_plan.md`

`JournalRemote` использует Qt Network и libsql/sqld endpoint:

```text
POST <baseUrl>/v2/pipeline
```

Remote дата пишется в формате `dd.MM.yyyy`, а month pattern выглядит как
`__.MM.YYYY`. SQL формируется строками, значения экранируются через
`JournalRemote::sqlQuote`.

Настройки по умолчанию:
- `JOURNAL_DEFAULT_SERVER_URL`: `http://127.0.0.1:8080`
- `JOURNAL_REMOTE_TIMEOUT_MS`: `5000`

Их можно переопределить переменными окружения:
- `JOURNAL_STORAGE_MODE=local|server`
- `JOURNAL_SERVER_URL=http://127.0.0.1:8080`

Для задач про сервер, ошибки подключения, timeout, JSON protocol или формат
remote SQL начинать с `JournalRemote.cpp`.

### Синхронизация

Основные файлы:
- `App/Inc/SyncService.hpp`
- `App/Src/SyncService.cpp`
- `App/Src/mainwindow.cpp` (`pushCurrentMonthToServer`, `pullCurrentMonthFromServer`)

Текущая стратегия простая: полная перезапись месяца.
- Push: собранные из UI local данные сохраняются на сервер.
- Pull: remote месяц читается и полностью сохраняется в локальный storage.

Конфликтов, ревизий и merge-логики сейчас нет; они описаны как будущий шаг в
`doc/server_sync_plan.md`.

## Что считается активным, а что наследием

Активные файлы текущей архитектуры:
- `main.cpp`
- `mainwindow.*`
- `JournalApp.*`
- `IJournalStorage.hpp`
- `JournalLocal.*`
- `JournalRemote.*`
- `SyncService.*`
- `SqliteConnect.*`
- `config.h`
- `journal_app.ui`

Устаревшие или переходные файлы:
- `dbManager.*`
- `mainTableManager.*`
- `checkTableManager.*`

Они все еще включены в `CMakeLists.txt`, но основной runtime-поток идет через
`MainWindow -> JournalApp -> IJournalStorage -> JournalLocal/JournalRemote`.
Перед удалением legacy-файлов нужно сначала проверить, нет ли скрытых ссылок в
UI, CMake или будущих ветках.

## Типовые точки входа для будущих задач

```text
Новая кнопка или изменение UI:
  journal_app.ui
  App/Inc/mainwindow.hpp
  App/Src/mainwindow.cpp

Изменить поведение add/delete/save/load:
  App/Inc/JournalApp.hpp
  App/Src/JournalApp.cpp
  App/Inc/IJournalStorage.hpp, если меняется контракт

Изменить локальную БД или схему:
  App/Inc/config.h
  App/Inc/SqliteConnect.hpp
  App/Src/SqliteConnect.cpp
  App/Src/JournalLocal.cpp

Изменить remote/server работу:
  App/Inc/JournalRemote.hpp
  App/Src/JournalRemote.cpp
  doc/docker_setup.md
  doc/server_sync_plan.md

Изменить sync local <-> server:
  App/Inc/SyncService.hpp
  App/Src/SyncService.cpp
  App/Src/mainwindow.cpp

Изменить сборку или зависимости:
  CMakeLists.txt
  doc/build_setup.md

Обновить пользовательское описание:
  README.md
```

## Важные текущие несоответствия

- `README.md` говорит, что данные хранятся в файловой системе; фактически
  активный путь использует SQLite и optional remote libsql.
- `doc/arch.md` описывает план и минимальную структуру, но текущий проект уже
  содержит `JournalRemote`, `SyncService` и legacy managers.
- Локальный и remote storage используют разные строковые форматы даты:
  local `dd.MM`, remote `dd.MM.yyyy`.
- Remote режим в UI помечен как `REMOTE (read-only)`, а edit controls
  отключаются, но `JournalRemote` сам реализует write-методы.
- `dbManager.*`, `mainTableManager.*`, `checkTableManager.*` выглядят как
  остатки старой реализации и не являются основным путем разработки.

