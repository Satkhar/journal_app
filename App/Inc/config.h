#pragma once

// Локальная SQLite база по умолчанию.
#define DB_FILENAME "test_data.db"
// Имя отдельной БД турниров в QStandardPaths::AppLocalDataLocation.
#define EVENT_DB_FILENAME "events_data.db"
// Историческая константа максимума дней; текущий код считает месяц через
// QDate.
#define MAX_DAYS 31

// Runtime options for storage backend:
// JOURNAL_STORAGE_MODE=local|server
// JOURNAL_SERVER_URL=http://127.0.0.1:8080
// JOURNAL_SERVER_TOKEN=<bearer token>
// JOURNAL_ALLOW_INSECURE_HTTP=1  // только для осознанного LAN PoC
// JOURNAL_BOOTSTRAP_REMOTE_SCHEMA=1  // только на первом локальном запуске
// Если переменные окружения не заданы, приложение стартует в local режиме.
#define JOURNAL_DEFAULT_STORAGE_MODE "local"
#define JOURNAL_DEFAULT_SERVER_URL "http://127.0.0.1:8080"
// Единый таймаут для всех HTTP-операций remote storage.
#define JOURNAL_REMOTE_TIMEOUT_MS 5000
