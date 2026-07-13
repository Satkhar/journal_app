#pragma once

#include <memory>

#include "IJournalStorage.hpp"

struct CopyUsersResult
{
  bool ok;
  int copied;
  int skipped;
  QString errorMessage;
};

class JournalApp
{
public:
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage,
                      bool allowBootstrapWrites = true);

  MonthSnapshot loadMonth(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  CopyUsersResult copyUsersFromMonth(int fromYear, int fromMonth, int toYear,
                                     int toMonth, bool copyActiveDays);
  bool addUser(const QString& name);
  bool removeParticipant(const ParticipantId& id);
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data);

private:
  std::unique_ptr<IJournalStorage> storage_;
  bool allowBootstrapWrites_;
  int currentYear_;
  int currentMonth_;
};
