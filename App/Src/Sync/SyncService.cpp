#include "SyncService.hpp"

#include "JournalRemote.hpp"

SyncService::SyncService(int timeoutMs) : timeoutMs_(timeoutMs) {}

bool SyncService::pushMonthToServer(const QString& serverUrl, int year,
                                    int month,
                                    const MonthSnapshot& localSnapshot,
                                    QString* errorMessage) const
{
  JournalRemote remote(serverUrl, timeoutMs_);
  if (!remote.connect(errorMessage))
  {
    return false;
  }
  if (!remote.replaceMonth(year, month, localSnapshot))
  {
    if (errorMessage)
    {
      *errorMessage = remote.lastError();
    }
    return false;
  }
  return true;
}

bool SyncService::pullMonthToLocal(const QString& serverUrl, int year,
                                   int month, IJournalStorage& localStorage,
                                   QString* errorMessage) const
{
  JournalRemote remote(serverUrl, timeoutMs_);
  if (!remote.connect(errorMessage))
  {
    return false;
  }

  MonthSnapshot snapshot;
  if (!remote.getMonthSnapshot(year, month, &snapshot))
  {
    if (errorMessage)
    {
      *errorMessage = remote.lastError();
    }
    return false;
  }
  if (!localStorage.replaceMonth(year, month, snapshot))
  {
    if (errorMessage && errorMessage->isEmpty())
    {
      *errorMessage = "Failed to replace local month";
    }
    return false;
  }
  return true;
}
