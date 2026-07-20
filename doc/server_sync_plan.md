# План серверной синхронизации

## Архитектурное решение

Приложение остаётся local-first:

```text
Qt UI -> JournalApp -> local SQLite
Qt UI -> SyncService -> IMonthSnapshotStore -> remote gateway
```

Локальная база — рабочая копия редактора. Целевой сервер должен хранить
versioned aggregates и принимать sync. Прямой remote-view допустим только как
read-only диагностика PoC. Смешанный режим с двумя равноправными источниками
запрещён: он не задаёт, чьи данные authoritative при расхождении.

## Что готово

- стабильные UUID участников;
- local schema v10 и транзакционные aggregate writes;
- атомарный `loadMonthSnapshot()` для local и remote;
- узкий `IMonthSnapshotStore`, инъецируемый в `SyncService`;
- full snapshot push/pull с сохранением local при remote read error;
- endpoint validation, optional bearer token, запрет внешнего plain HTTP;
- localhost-only pinned Docker Compose;
- fake-based unit tests sync policy.

## Ограничения текущего PoC

- month snapshot не имеет revision; concurrent push использует
  last-writer-wins;
- remote transport синхронный, содержит nested `QEventLoop` и блокирует GUI;
- desktop adapter отправляет raw SQL в Hrana endpoint;
- карточки участников синхронизируются неполно; турниры не синхронизируются;
- bootstrap/migrations ещё выполняет desktop adapter при явном флаге;
- Compose не имеет auth/TLS и разрешён только на loopback.

Пока эти ограничения существуют, сервер нельзя публиковать в LAN/интернет и
нельзя использовать несколькими writers с ценными данными.

## Этап 1: локальный single-writer PoC

1. Поднять Compose по `doc/docker_setup.md`.
2. Проверить `/v2/pipeline` запросом `SELECT 1`.
3. Один раз запустить client с
   `JOURNAL_BOOTSTRAP_REMOTE_SCHEMA=1`, затем убрать флаг.
4. Проверить push/pull отдельного тестового месяца.
5. Проверить backup, restore и reset volume.

## Этап 2: защита от потери данных

1. Добавить `month_revision` в local/remote model.
2. Читать `VersionedMonth {snapshot, revision}` одной транзакцией.
3. Заменять месяц только при `expectedRevision` через CAS.
4. Возвращать typed `Conflict`, не строку `lastError()`.
5. Добавить integration test двух клиентов: stale writer обязан получить
   conflict, а не успешный overwrite.

Без этого этапа multi-client запрещён.

## Этап 3: production transport boundary

1. Вынести SQL и migrations на server side.
2. Desktop должен вызывать domain HTTP API с DTO, не raw SQL endpoint.
3. Добавить TLS, auth, scopes, secret storage, request size/rate limits.
4. Сделать client async с cancellation, deadlines и typed network errors.
5. Не использовать `QSqlDatabase`/`QNetworkAccessManager` вне owning thread.

## Этап 4: остальные aggregates

Версионировать и синхронизировать отдельно:

- каталог/карточки участников;
- турниры и бои;
- вложения, если появятся.

Разные aggregates не объединять в один гигантский month payload: у них разные
lifetime, conflict policy и права доступа.

## Этап 5: эксплуатация

- автоматические миграции deployment job;
- backup/restore drill;
- health/readiness endpoints;
- structured logs, metrics, alerts;
- server contract tests и pinned integration environment;
- политика retention и защита персональных данных.
