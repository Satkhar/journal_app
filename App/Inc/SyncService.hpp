#pragma once

#include <QString>

#include <vector>

#include "IJournalStorage.hpp"

class SyncService {
 public:
  explicit SyncService(int timeoutMs = 5000);

  bool pushMonthToServer(const QString& serverUrl, int year, int month,
                         const std::vector<AttendanceRecord>& localData,
                         QString* errorMessage = nullptr) const;

  bool pullMonthToLocal(const QString& serverUrl, int year, int month,
                        IJournalStorage& localStorage,
                        QString* errorMessage = nullptr) const;

 private:
  int timeoutMs_;
};

