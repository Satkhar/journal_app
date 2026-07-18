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
// JOURNAL_SERVER_URL=http://127.0.0.1:7070
// Если переменные окружения не заданы, приложение стартует в local режиме.
#define JOURNAL_DEFAULT_STORAGE_MODE "local"
#define JOURNAL_DEFAULT_SERVER_URL "http://127.0.0.1:7070"
// Единый таймаут для всех HTTP-операций remote storage.
#define JOURNAL_REMOTE_TIMEOUT_MS 5000
