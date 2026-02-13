# Docker Setup (libsql server)

Документ описывает:
- как установить Docker на Windows,
- как поднять `libsql/sqld` для проекта,
- как выполнять базовые операции и диагностику.

## 1. Установка Docker Desktop (Windows)

### Вариант A: через winget (рекомендуется)
```powershell
winget install --id Docker.DockerDesktop -e --accept-source-agreements --accept-package-agreements
```

### Вариант B: через GUI
1. Скачать Docker Desktop: https://www.docker.com/products/docker-desktop/
2. Установить обычным инсталлятором.

## 2. Первый запуск после установки

1. Запустить `Docker Desktop`.
2. Дождаться статуса `Engine running`.
3. Проверить в новом терминале:
```powershell
docker --version
docker compose version
docker info
```

Если `docker` не найден:
1. Полностью перезапустить VS Code/терминал.
2. Временно обновить PATH в текущей сессии:
```powershell
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
```

## 3. Подготовка директории сервера

```powershell
mkdir D:\Work\libsql-server
cd D:\Work\libsql-server
```

Создать файл `docker-compose.yml`:

```yaml
services:
  sqld:
    image: ghcr.io/tursodatabase/libsql-server:latest
    platform: linux/amd64
    ports:
      - "8080:8080"
      - "5001:5001"
    environment:
      - SQLD_NODE=primary
    volumes:
      - ./data/libsql:/var/lib/sqld
```

## 4. Запуск и остановка сервера

Рабочая директория:
```powershell
cd D:\Work\libsql-server
```

Запуск в фоне:
```powershell
docker compose up -d
```

Проверка статуса:
```powershell
docker compose ps
```

Логи:
```powershell
docker compose logs -f
```

Остановка (без удаления):
```powershell
docker compose stop
```

Повторный запуск после stop:
```powershell
docker compose start
```

Остановка и удаление контейнера/сети:
```powershell
docker compose down
```

Полный reset (включая удаление данных):
```powershell
docker compose down -v
```

## 5. Куда подключать приложение

Для `JournalRemote` используйте:
```text
http://127.0.0.1:8080
```
или
```text
http://localhost:8080
```

## 6. Где лежат данные сервера

Данные сохраняются на хосте в:
```text
D:\Work\libsql-server\data\libsql
```

Это важно для backup/restore.

## 7. Частые проблемы

### 1) `docker: The term 'docker' is not recognized`
- Docker установлен, но терминал не подхватил новый PATH.
- Решение: перезапустить VS Code/терминал или обновить PATH в текущей сессии.

### 2) `error getting credentials ... docker-credential-desktop`
- Обычно PATH не содержит `C:\Program Files\Docker\Docker\resources\bin`.
- Решение: перезапуск среды или ручное добавление пути в PATH.

### 3) Контейнер поднялся, но сервер не отвечает
1. Проверить статус:
```powershell
docker compose ps
```
2. Посмотреть логи:
```powershell
docker compose logs -f
```
3. Убедиться, что порт `8080` не занят другим процессом.

