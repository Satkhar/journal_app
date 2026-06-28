#include "SyncService.hpp"

#include "JournalRemote.hpp"

SyncService::SyncService(int timeoutMs) : timeoutMs_(timeoutMs) {}

bool SyncService::pushMonthToServer(
    const QString& serverUrl, int year, int month,
    const std::vector<AttendanceRecord>& localData,
    QString* errorMessage) const {
  // На каждую sync-операцию создаем новый remote-клиент.
  // Это упрощает управление состоянием и исключает зависимость от UI-сессии.
  auto remote = std::make_unique<JournalRemote>(serverUrl, timeoutMs_);
  if (!remote->connect(errorMessage)) {
    return false;
  }

  // Для PoC push реализован как "полная перезапись месяца" на сервере.
  if (!remote->saveMonth(year, month, localData)) {
    if (errorMessage && errorMessage->isEmpty()) {
      *errorMessage = "Failed to save month on remote server";
    }
    return false;
  }

  return true;
}

bool SyncService::pullMonthToLocal(const QString& serverUrl, int year, int month,
                                   IJournalStorage& localStorage,
                                   QString* errorMessage) const {
  // Читаем серверный срез месяца и переносим его в local storage.
  auto remote = std::make_unique<JournalRemote>(serverUrl, timeoutMs_);
  if (!remote->connect(errorMessage)) {
    return false;
  }

  const std::vector<AttendanceRecord> remoteData = remote->getMonth(year, month);
  // Критично: если чтение с сервера завершилось ошибкой, локальные данные не трогаем.
  if (!remote->lastError().isEmpty()) {
    if (errorMessage) {
      *errorMessage = remote->lastError();
    }
    return false;
  }

  // Запись в local storage выполняется уже через переданный адаптер:
  // SyncService не знает, SQLite это или другой локальный backend.
  if (!localStorage.saveMonth(year, month, remoteData)) {
    if (errorMessage && errorMessage->isEmpty()) {
      *errorMessage = "Failed to save remote month to local storage";
    }
    return false;
  }

  return true;
}
