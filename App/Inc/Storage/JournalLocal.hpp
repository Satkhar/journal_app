#pragma once

#include <memory>

#include "IJournalStorage.hpp"
#include "SqliteConnect.hpp"

class JournalLocal : public IJournalStorage
{
public:
  explicit JournalLocal(std::unique_ptr<SqliteConnect> sqlite);

  std::vector<Participant> getParticipantsForMonth(int year,
                                                   int month) override;
  QVector<int> getActiveDays(int year, int month) override;
  bool saveActiveDays(int year, int month, const QVector<int>& days) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data) override;
  bool addParticipantToMonth(int year, int month,
                             const Participant& participant) override;
  bool removeParticipantFromMonth(int year, int month,
                                  const ParticipantId& id) override;
  bool replaceMonth(int year, int month,
                    const MonthSnapshot& snapshot) override;

private:
  std::unique_ptr<SqliteConnect> sqlite_;
};
