#include "SyncService.hpp"

bool SyncService::pushMonthToServer(IMonthSnapshotStore& remote, int year,
                                    int month,
                                    const MonthSnapshot& localSnapshot,
                                    QString* errorMessage) const
{
  if (errorMessage)
  {
    errorMessage->clear();
  }
  if (localSnapshot.state != MonthState::Ready)
  {
    if (errorMessage)
    {
      *errorMessage = "Local month snapshot is not ready";
    }
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

bool SyncService::pullMonthToLocal(IMonthSnapshotStore& remote, int year,
                                   int month, IMonthSnapshotStore& local,
                                   QString* errorMessage) const
{
  if (errorMessage)
  {
    errorMessage->clear();
  }
  const MonthSnapshot snapshot = remote.loadMonthSnapshot(year, month);
  if (snapshot.state != MonthState::Ready)
  {
    if (errorMessage)
    {
      *errorMessage =
          snapshot.state == MonthState::Error
              ? (snapshot.errorMessage.isEmpty() ? remote.lastError()
                                                 : snapshot.errorMessage)
              : "Remote month is missing; local month was not modified";
    }
    return false;
  }

  // Remote read завершается до local write. Любая remote ошибка оставляет
  // локальный aggregate неизменным.
  if (!local.replaceMonth(year, month, snapshot))
  {
    if (errorMessage)
    {
      *errorMessage = local.lastError().isEmpty()
                          ? "Failed to replace local month"
                          : local.lastError();
    }
    return false;
  }
  return true;
}
