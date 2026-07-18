# Навигация по проекту

Документ описывает текущий код. Архитектурные contracts и server blockers:
`doc/arch.md`, `doc/server_sync_plan.md`.

## Runtime flow

```text
main.cpp
  MainWindow
    JournalApp
      IJournalStorage
        JournalLocal -> SqliteConnect -> local SQLite

    SyncService
      IMonthSnapshotStore
        JournalLocal / JournalRemote -> libSQL Hrana HTTP

    EventApp
      IEventStorage
        EventSqliteStorage -> events SQLite
```

Рабочая модель local-first. `JournalApp` получает месяц явно в каждой команде;
скрытого current month в use-case layer нет. `loadMonth()` читает один атомарный
snapshot. `SyncService` не создаёт HTTP adapter и не знает URL/auth: готовые
stores передаются через `IMonthSnapshotStore`.

## Каталоги

```text
App/
  Inc/
    Domain/
      JournalModels.hpp          domain models, MonthSnapshot
      IMonthSnapshotStore.hpp    узкий atomic snapshot contract для sync
      IJournalStorage.hpp        month/profile editor repository
      JournalApp.hpp             use cases
      EventModels.hpp
      IEventStorage.hpp
      EventApp.hpp
    Storage/
      SqliteConnect.hpp          local journal SQLite
      JournalLocal.hpp           IJournalStorage adapter
      JournalRemote.hpp          synchronous Hrana PoC adapter
      RemoteConnectionOptions.hpp URL/auth/transport validation
      EventSqliteStorage.hpp     tournaments SQLite + revision/CAS
    Sync/
      SyncService.hpp            snapshot push/pull policy
    Ui/                           Qt Widgets declarations
    config.h                     DB names, default endpoint, timeout
  Src/                           implementations matching Inc

Test/
  JournalStorageTests.cpp        journal schema/storage
  EventStorageTests.cpp          tournament aggregate/schema
  month_state_tests.cpp          month/use-case semantics
  SyncServiceTests.cpp           fake gateway + endpoint policy
  ui_dialog_tests.cpp            widget/dialog behavior

deploy/libsql/compose.yaml       pinned localhost-only server PoC
doc/docker_setup.md              run/backup/restore/reset runbook
doc/server_sync_plan.md          phases to production server
doc/arch.md                      current contracts and schema
```

## Где менять

### Month/domain behavior

- `App/Inc/Domain/JournalModels.hpp`
- `App/Inc/Domain/JournalApp.hpp`
- `App/Src/Domain/JournalApp.cpp`
- `Test/month_state_tests.cpp`

### Storage contract

- editor CRUD: `IJournalStorage.hpp`;
- full aggregate sync: `IMonthSnapshotStore.hpp`;
- local: `SqliteConnect.*`, `JournalLocal.*`;
- remote: `JournalRemote.*`, `RemoteConnectionOptions.*`.

Snapshot read обязан быть одной logical read transaction. Error возвращает
`MonthState::Error`, не частичные данные. `replaceMonth()` обязан быть atomic.
До month revision/CAS remote replace остаётся single-writer PoC.

### UI/month table

- `journal_app.ui`
- `App/Inc/Ui/mainwindow.hpp`
- `App/Src/Ui/mainwindow.cpp`
- `AttendanceCellWidget.*`
- соответствующие dialogs

`MainWindow` пока также composition root. Новую storage/network policy туда не
добавлять: выносить в use case/interface. Долгосрочно таблице нужен
`QAbstractTableModel + QTableView`, если число участников существенно вырастет.

### Server/sync

- `App/Inc/Domain/IMonthSnapshotStore.hpp`
- `App/Inc/Storage/RemoteConnectionOptions.hpp`
- `App/Src/Storage/JournalRemote.cpp`
- `App/Inc|Src/Sync/SyncService.*`
- `Test/SyncServiceTests.cpp`
- `deploy/libsql/compose.yaml`
- `doc/docker_setup.md`
- `doc/server_sync_plan.md`

Default endpoint: `http://127.0.0.1:8080`.

Environment:

- `JOURNAL_STORAGE_MODE=local|server` — direct remote mode только диагностика;
- `JOURNAL_SERVER_URL` — absolute HTTP(S) URL без credentials/query/fragment;
- `JOURNAL_SERVER_TOKEN` — optional bearer token, не показывается в UI;
- `JOURNAL_ALLOW_INSECURE_HTTP=1` — explicit LAN PoC override;
- `JOURNAL_BOOTSTRAP_REMOTE_SCHEMA=1` — explicit one-time DDL/migration;
- `JOURNAL_REMOTE_TIMEOUT_MS` задаётся compile-time в `config.h`.

Plain HTTP без override разрешён только loopback. Redirect запрещён, чтобы
Bearer token не ушёл другому origin.

## Проверка

```powershell
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
docker compose -f deploy/libsql/compose.yaml config
```

Обычный `ctest` не требует сети/Docker. Настоящие remote integration tests ещё
нужны: fresh schema, malformed response, transaction rollback, stale revision
conflict, retry/idempotency.
