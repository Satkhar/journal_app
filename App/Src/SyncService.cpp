#include "SyncService.hpp"

#include "JournalRemote.hpp"

SyncService::SyncService(int timeoutMs) : timeoutMs_(timeoutMs) {}

bool SyncService::pushMonthToServer(
    const QString& serverUrl, int year, int month,
    const std::vector<AttendanceRecord>& localData,
    QString* errorMessage) const {
  auto remote = std::make_unique<JournalRemote>(serverUrl, timeoutMs_);
  if (!remote->connect(errorMessage)) {
    return false;
  }

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
  auto remote = std::make_unique<JournalRemote>(serverUrl, timeoutMs_);
  if (!remote->connect(errorMessage)) {
    return false;
  }

  const std::vector<AttendanceRecord> remoteData = remote->getMonth(year, month);
  if (!localStorage.saveMonth(year, month, remoteData)) {
    if (errorMessage && errorMessage->isEmpty()) {
      *errorMessage = "Failed to save remote month to local storage";
    }
    return false;
  }

  return true;
}

