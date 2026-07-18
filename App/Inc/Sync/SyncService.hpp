#pragma once

#include <QString>

#include "IMonthSnapshotStore.hpp"

class SyncService
{
public:
  // Stores передаются готовыми к работе. Это отделяет sync policy от HTTP,
  // URL, auth и SQLite и позволяет проверять policy test doubles.
  bool pushMonthToServer(IMonthSnapshotStore& remote, int year, int month,
                         const MonthSnapshot& localSnapshot,
                         QString* errorMessage = nullptr) const;
  bool pullMonthToLocal(IMonthSnapshotStore& remote, int year, int month,
                        IMonthSnapshotStore& local,
                        QString* errorMessage = nullptr) const;
};
