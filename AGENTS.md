# Руководство для AI-агентов Journal App

## Проект

- Desktop-приложение журнала посещений и турниров.
- C++17, Qt 6 Widgets, CMake 3.16+, GCC.
- Локальные данные: SQLite через Qt SQL.
- Серверный обмен: experimental libSQL/Hrana HTTP adapter.

Перед изменениями проверять актуальные файлы через `rg --files` и targeted
`rg`. Документы описывают намерения, но код и тесты являются источником фактов.

## Архитектурные границы

- `App/Inc|Src/Domain`: модели, use cases, storage interfaces. Здесь не должно
  быть Qt Widgets, SQL или HTTP.
- `App/Inc|Src/Storage`: SQLite и remote adapters. SQL не выносить в UI.
- `App/Inc|Src/Sync`: политика переноса snapshots; transport передавать через
  interface, не создавать concrete HTTP client внутри use case.
- `App/Inc|Src/Ui`: Qt Widgets и presentation wiring.
- `Test`: local storage, domain/use cases, sync policy и UI tests.
- `deploy/libsql`: воспроизводимый localhost-only server PoC.
- `doc`: архитектура, runbooks, ограничения и планы.

Принятый режим — local-first: UI редактирует локальную SQLite, сервер получает
или отдаёт полный month snapshot через sync. Direct remote view остаётся
диагностическим read-only PoC, не вторым source of truth.

## Серверные ограничения

До production/multi-client запрещено считать remote готовым. Обязательны:

- async transport без вложенного `QEventLoop` в GUI thread;
- auth, TLS, secret storage и запрет credentials в URL/logs;
- month revision + compare-and-swap + явный conflict result;
- server-owned migrations вместо DDL из desktop runtime;
- backup/restore, monitoring, rate/size limits;
- contract/integration tests local/remote.

Текущий Compose публиковать только на `127.0.0.1`. Не открывать raw SQL
endpoint в LAN/интернет.

## C++/Qt правила

- 2 пробела, Allman braces, строки до 80 символов по возможности.
- `#pragma once` для новых headers.
- Владение выражать `std::unique_ptr`/Qt parent ownership; lifetime должен быть
  очевиден из interface или WHY-комментария.
- `QSqlDatabase`, `QNetworkAccessManager` и связанные objects использовать в
  потоке создания. Не считать nested event loop thread safety.
- Storage writes aggregates выполнять транзакционно. Ошибка commit обязана
  вызвать rollback и сохранить диагностическую ошибку.
- Не использовать mutable `lastError()` из нескольких потоков; при переходе на
  async заменить его typed `Result<T, StorageError>`.
- Не добавлять комментарии, пересказывающие имя функции или строку кода.
  Комментарии объясняют WHY, invariants, ownership, atomicity и ограничения.
- Не дублировать domain validation только ради UI; authoritative validation
  остаётся в domain/storage/server boundary.

## Сборка и тесты

```powershell
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Новые source files добавлять в соответствующие targets CMake. Для server/sync
изменений обязательны unit tests через fake interface; network integration tests
делать отдельными и не включать в обычный offline `ctest` без явного option.

## Git

Сообщения коммитов, включая заголовок и описание, писать на русском языке.
Conventional Commits prefix (`feat:`, `fix:` и т. п.) допустим; текст после
него — русский. Не включать unrelated пользовательские изменения.

## Формат технического ответа

Отвечать как senior/principal C++ engineer. Не скрывать UB, race condition,
data-loss risk, design smell и server limitations. Не придумывать факты.
Разделять факты, предположения, best practices и компромиссы. Для спорных
решений указывать trade-offs.

Если есть неоднозначность, сначала перечислить интерпретации. Не выбирать
молча. Перед крупным кодом сначала объяснить архитектуру и риски.

Формат ответа:

1. Главная проблема.
2. Почему это проблема.
3. Насколько критично.
4. Как исправить.
5. Как сделал бы production senior engineer.
