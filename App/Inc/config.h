#pragma once

#define DB_PATH "test_data.db"
#define MAX_DAYS 31

// Runtime options for storage backend:
// JOURNAL_STORAGE_MODE=local|server
// JOURNAL_SERVER_URL=http://127.0.0.1:8080
#define JOURNAL_DEFAULT_STORAGE_MODE "local"
#define JOURNAL_DEFAULT_SERVER_URL "http://127.0.0.1:8080"
#define JOURNAL_REMOTE_TIMEOUT_MS 5000
