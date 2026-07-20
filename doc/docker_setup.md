# Локальный сервер libSQL

Эта конфигурация поднимает воспроизводимый libSQL PoC только на текущем
компьютере. Все команды ниже выполняются из корня репозитория.

## Требования

Нужен запущенный Docker Desktop с поддержкой Compose. Проверка:

```powershell
docker --version
docker compose version
docker info
```

Если команда `docker` не появилась после установки Docker Desktop,
перезапустите терминал или IDE.

## Запуск

```powershell
docker compose -f deploy/libsql/compose.yaml pull
docker compose -f deploy/libsql/compose.yaml up -d
docker compose -f deploy/libsql/compose.yaml ps
```

Сервер доступен только через `127.0.0.1:8080`. gRPC-порт наружу не
публикуется.

Логи:

```powershell
docker compose -f deploy/libsql/compose.yaml logs -f libsql
```

Остановка без удаления контейнера и данных:

```powershell
docker compose -f deploy/libsql/compose.yaml stop
```

Повторный запуск:

```powershell
docker compose -f deploy/libsql/compose.yaml start
```

Удаление контейнера и сети с сохранением данных:

```powershell
docker compose -f deploy/libsql/compose.yaml down
```

## Проверка HTTP API

Запрос к Hrana HTTP pipeline должен вернуть JSON с результатом `SELECT 1`:

```powershell
curl.exe -sS -X POST http://127.0.0.1:8080/v2/pipeline `
  -H "Content-Type: application/json" `
  --data-binary '{"baton":null,"requests":[{"type":"execute","stmt":{"sql":"SELECT 1 AS ok","args":[],"named_args":[],"want_rows":true}},{"type":"close"}]}'
```

Если сервер не отвечает, проверьте состояние и логи:

```powershell
docker compose -f deploy/libsql/compose.yaml ps
docker compose -f deploy/libsql/compose.yaml logs libsql
```

## Подключение приложения

Перед запуском приложения задайте адрес в том же терминале:

```powershell
$env:JOURNAL_SERVER_URL = 'http://127.0.0.1:8080'
```

Значение применяется только к процессам, запущенным из этого терминала.

Для пустого server volume разрешите одноразовое создание schema:

```powershell
$env:JOURNAL_BOOTSTRAP_REMOTE_SCHEMA = '1'
# Запустите приложение и один раз подключитесь к серверу.
Remove-Item Env:JOURNAL_BOOTSTRAP_REMOTE_SCHEMA
```

Без этого флага desktop только проверяет schema v9. Флаг даёт клиенту DDL-права
и не должен оставаться в обычной эксплуатации.

## Данные, backup и restore

Данные хранятся в именованном Docker volume
`journal_app_libsql_data`. Для согласованной копии сначала остановите сервер.

Создание архива `libsql-backup.tar.gz` в корне репозитория:

```powershell
docker compose -f deploy/libsql/compose.yaml stop
docker run --rm `
  -v journal_app_libsql_data:/source:ro `
  -v "${PWD}:/backup" `
  alpine:3.22 tar -czf /backup/libsql-backup.tar.gz -C /source .
docker compose -f deploy/libsql/compose.yaml start
```

Восстановление архива полностью заменяет текущие серверные данные:

```powershell
docker compose -f deploy/libsql/compose.yaml down
docker volume rm journal_app_libsql_data
docker volume create journal_app_libsql_data
docker run --rm `
  -v journal_app_libsql_data:/target `
  -v "${PWD}:/backup:ro" `
  alpine:3.22 sh -c "cd /target && tar -xzf /backup/libsql-backup.tar.gz"
docker compose -f deploy/libsql/compose.yaml up -d
```

Полный reset без сохранения данных:

```powershell
docker compose -f deploy/libsql/compose.yaml down -v
docker compose -f deploy/libsql/compose.yaml up -d
```

`down` без `-v` сохраняет базу. `down -v` необратимо удаляет именованный
volume и все данные libSQL.

## Ограничения PoC

- Нет аутентификации и разграничения доступа.
- Нет TLS: HTTP-трафик не шифруется.
- Версия/форма schema проверяются, но migration job пока не отделён от клиента.
- Это один primary-узел без репликации, failover и мониторинга.
- Backup выполняется вручную с остановленным сервером.

Конфигурацию нельзя публиковать в LAN или интернет. Привязка порта
`127.0.0.1:8080:8080` обязательна для этого PoC; не заменяйте её на
`8080:8080` или `0.0.0.0:8080:8080`. Для удалённого сервера сначала нужны
аутентификация, TLS, управление секретами, миграции/ревизии схемы,
резервное копирование и восстановление, наблюдаемость и ограничение сети.
