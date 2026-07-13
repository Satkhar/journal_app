> **Статус:** документ описывает schema v1 и сохранен как historical reference. Актуальная доменная модель и schema v3: doc/arch.md. План карточек: doc/participant_profiles_plan.md.

# Project Navigation

Этот документ фиксирует текущую карту проекта для быстрой навигации перед
следующими задачами. Это не план развития, а снимок того, что где лежит и какие
файлы обычно нужно открывать для разных типов изменений.

## Кратко о проекте

`journal_app` - desktop-приложение на C++17 / Qt6 Widgets для ведения месячного
журнала посещаемости. Основная таблица показывает пользователей строками и дни
месяца колонками. Данные можно хранить локально в SQLite или читать/писать через
remote storage на базе libsql/sqld HTTP API.

В local-режиме для каждого месяца можно настроить, какие дни входят в учет.
Если настройка для месяца отсутствует, приложение показывает весь календарный
месяц, как в старом поведении.

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
      Domain/
        IJournalStorage.hpp   # общий контракт local/remote storage и модели
        JournalApp.hpp        # use-case слой приложения
      Storage/
        JournalLocal.hpp      # адаптер IJournalStorage -> SqliteConnect
        JournalRemote.hpp     # remote storage через libsql HTTP pipeline
        SqliteConnect.hpp     # низкоуровневый SQLite storage
      Sync/
        SyncService.hpp       # push/pull месяца между local и remote
      Ui/
        mainwindow.hpp        # главный Qt UI-контроллер
        MonthDaysDialog.hpp   # диалог выбора дней учета месяца
        CopyUsersDialog.hpp   # диалог выбора месяца-источника для переноса пользователей

    Src/
      main.cpp                # QApplication, MainWindow, Windows UTF-8 console
      Domain/
        JournalApp.cpp        # сценарии load/add/delete/save для выбранного месяца
      Storage/
        JournalLocal.cpp      # тонкий adapter к SqliteConnect
        JournalRemote.cpp     # SQL-over-HTTP к libsql/sqld /v2/pipeline
        SqliteConnect.cpp     # SQLite schema и CRUD месяца
      Sync/
        SyncService.cpp       # push local->server и pull server->local
      Ui/
        mainwindow.cpp        # UI wiring, таблица, режимы local/remote, sync buttons
        MonthDaysDialog.cpp   # календарный диалог выбора дней учета
        CopyUsersDialog.cpp   # диалог переноса пользователей из другого месяца

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
2. `MainWindow` в `App/Src/Ui/mainwindow.cpp`:
   - загружает UI из `journal_app.ui` через `Ui::MainWindow`;
   - создает таблицы `bigTable` и скрытую `checkTable`;
   - создает панели действий `Подключение`, `Текущий месяц`, `Данные`;
   - создает controls для выбора `Local` / `Remote`;
   - инициализирует `JournalApp` с нужной реализацией `IJournalStorage`;
   - связывает кнопки с add/delete/read/save/push/pull/copy действиями.
3. `JournalApp` в `App/Src/Domain/JournalApp.cpp` хранит выбранный месяц, выполняет month/profile use cases и не делает bootstrap-записей.
4. `IJournalStorage` задает общий контракт:
   - `getUsersForMonth`
   - `getActiveDays`
   - `saveActiveDays`
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

Основной файл: `App/Src/Ui/mainwindow.cpp`.

Здесь находятся:
- создание и перерисовка таблицы месяца (`createEmptyTable`, `renderMonth`);
- чтение чекбоксов из таблицы (`collectMonthFromTable`);
- создание панелей действий (`setupActionPanels`, `setupConnectionPanel`,
  `setupMonthPanel`, `setupDataPanel`);
- вызов диалога настройки дней месяца (`configureMonthDays`);
- вызов диалога переноса пользователей (`copyUsersFromMonth`);
- обработчики кнопок `Add`, `Delete`, `Read Base`, `Save Current Table`,
  push/pull серверной синхронизации;
- переключение режимов storage (`setupStorage`, `connectLocalFromUi`,
  `connectRemoteFromUi`);
- статусная строка и badge текущего режима.

При изменениях в UX, кнопках, календаре, таблице или статусах обычно начинать с:
- `journal_app.ui`
- `App/Inc/Ui/mainwindow.hpp`
- `App/Src/Ui/mainwindow.cpp`
- `App/Inc/Ui/MonthDaysDialog.hpp`
- `App/Src/Ui/MonthDaysDialog.cpp`
- `App/Inc/Ui/CopyUsersDialog.hpp`
- `App/Src/Ui/CopyUsersDialog.cpp`

### Use-case слой

Основные файлы:
- `App/Inc/Domain/JournalApp.hpp`
- `App/Src/Domain/JournalApp.cpp`

Здесь должны жить сценарии приложения, не привязанные к конкретному UI-виджету:
загрузить месяц, добавить пользователя, удалить пользователя, сохранить месяц.

Если нужно менять бизнес-поведение, которое одинаково для local/remote, начинать
с `JournalApp`.

### Storage contract и модели

Основной файл: `App/Inc/Domain/IJournalStorage.hpp`.

Здесь определены:
- `AttendanceRecord`: `userName`, `day`, `isChecked`;
- active days месяца через `QVector<int>`;
- интерфейс `IJournalStorage`.

Если меняется доменная форма данных, дата, поля записи или контракт хранения,
начинать с этого файла, затем пройти реализации `JournalLocal`, `JournalRemote`,
`SqliteConnect` и места сборки таблицы в `MainWindow`.

### Local SQLite storage

Основные файлы:
- `App/Inc/Storage/SqliteConnect.hpp`
- `App/Src/Storage/SqliteConnect.cpp`
- `App/Inc/Storage/JournalLocal.hpp`
- `App/Src/Storage/JournalLocal.cpp`

Текущая SQLite-схема создается автоматически:

```sql
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  date TEXT NOT NULL,
  is_checked INTEGER NOT NULL
)

CREATE TABLE IF NOT EXISTS month_days (
  year INTEGER NOT NULL,
  month INTEGER NOT NULL,
  day INTEGER NOT NULL,
  PRIMARY KEY(year, month, day)
)
```

Локальная дата сейчас пишется в старом формате `dd.MM`, а чтение месяца ищет
`__.MM%`, чтобы сохранить совместимость со старой БД. Путь к БД задан в
`App/Inc/config.h` как `DB_PATH "test_data.db"`.

`month_days` хранит выбранные дни учета для local-месяца. Если записей для
`(year, month)` нет, `SqliteConnect::getActiveDays` возвращает полный месяц.
`SqliteConnect::saveActiveDays` сохраняет настройку, удаляет attendance по
выключенным дням и добавляет недостающие `false`-записи для существующих
пользователей по новым включенным дням.

Для задач про локальную БД, SQL, формат хранения и производительность сохранения
месяца начинать с `SqliteConnect.cpp`.

### Remote storage

Основные файлы:
- `App/Inc/Storage/JournalRemote.hpp`
- `App/Src/Storage/JournalRemote.cpp`
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
- `JOURNAL_DEFAULT_SERVER_URL`: `http://127.0.0.1:7070`
- `JOURNAL_REMOTE_TIMEOUT_MS`: `5000`

Их можно переопределить переменными окружения:
- `JOURNAL_STORAGE_MODE=local|server`
- `JOURNAL_SERVER_URL=http://127.0.0.1:7070`

Для задач про сервер, ошибки подключения, timeout, JSON protocol или формат
remote SQL начинать с `JournalRemote.cpp`.

### Синхронизация

Основные файлы:
- `App/Inc/Sync/SyncService.hpp`
- `App/Src/Sync/SyncService.cpp`
- `App/Src/Ui/mainwindow.cpp` (`pushCurrentMonthToServer`, `pullCurrentMonthFromServer`)

Текущая стратегия простая: полная перезапись месяца.
- Push: собранные из UI local данные сохраняются на сервер.
- Pull: remote месяц читается и полностью сохраняется в локальный storage.

Конфликтов, ревизий и merge-логики сейчас нет; они описаны как будущий шаг в
`doc/server_sync_plan.md`.

## Что считается активным

Активные файлы текущей архитектуры:
- `main.cpp`
- `Ui/mainwindow.*`
- `Ui/MonthDaysDialog.*`
- `Ui/CopyUsersDialog.*`
- `Domain/JournalApp.*`
- `Domain/IJournalStorage.hpp`
- `Storage/JournalLocal.*`
- `Storage/JournalRemote.*`
- `Sync/SyncService.*`
- `Storage/SqliteConnect.*`
- `config.h`
- `journal_app.ui`

Основной runtime-поток идет через
`MainWindow -> JournalApp -> IJournalStorage -> JournalLocal/JournalRemote`.

## Типовые точки входа для будущих задач

```text
Новая кнопка или изменение UI:
  journal_app.ui
  App/Inc/Ui/mainwindow.hpp
  App/Src/Ui/mainwindow.cpp
  App/Inc/Ui/MonthDaysDialog.hpp
  App/Src/Ui/MonthDaysDialog.cpp

Настройка дней учета месяца:
  App/Src/Ui/mainwindow.cpp
  App/Inc/Ui/MonthDaysDialog.hpp
  App/Src/Ui/MonthDaysDialog.cpp
  App/Inc/Domain/IJournalStorage.hpp
  App/Inc/Domain/JournalApp.hpp
  App/Src/Domain/JournalApp.cpp
  App/Inc/Storage/SqliteConnect.hpp
  App/Src/Storage/SqliteConnect.cpp

Перенос пользователей между месяцами:
  App/Inc/Ui/CopyUsersDialog.hpp
  App/Src/Ui/CopyUsersDialog.cpp
  App/Inc/Ui/mainwindow.hpp
  App/Src/Ui/mainwindow.cpp
  App/Inc/Domain/JournalApp.hpp
  App/Src/Domain/JournalApp.cpp

Изменить поведение add/delete/save/load:
  App/Inc/Domain/JournalApp.hpp
  App/Src/Domain/JournalApp.cpp
  App/Inc/Domain/IJournalStorage.hpp, если меняется контракт

Изменить локальную БД или схему:
  App/Inc/config.h
  App/Inc/Storage/SqliteConnect.hpp
  App/Src/Storage/SqliteConnect.cpp
  App/Src/Storage/JournalLocal.cpp

Изменить remote/server работу:
  App/Inc/Storage/JournalRemote.hpp
  App/Src/Storage/JournalRemote.cpp
  doc/docker_setup.md
  doc/server_sync_plan.md

Изменить sync local <-> server:
  App/Inc/Sync/SyncService.hpp
  App/Src/Sync/SyncService.cpp
  App/Src/Ui/mainwindow.cpp

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
  содержит `JournalRemote`, `SyncService` и настройку дней учета месяца.
- Локальный и remote storage используют разные строковые форматы даты:
  local `dd.MM`, remote `dd.MM.yyyy`.
- Настройка дней учета реализована только для local storage. Remote storage
  пока возвращает полный месяц и не сохраняет `activeDays`.
- Remote режим в UI помечен как `REMOTE (read-only)`, а edit controls
  отключаются, но `JournalRemote` сам реализует write-методы.

