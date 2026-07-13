#include "JournalLocal.hpp"

JournalLocal::JournalLocal(std::unique_ptr<SqliteConnect> sqlite)
    : sqlite_(std::move(sqlite))
{
}

std::vector<Participant> JournalLocal::getParticipantsForMonth(int year,
                                                               int month)
{
  return sqlite_->getParticipantsForMonth(year, month);
}

QVector<int> JournalLocal::getActiveDays(int year, int month)
{
  return sqlite_->getActiveDays(year, month);
}

bool JournalLocal::saveActiveDays(int year, int month, const QVector<int>& days)
{
  return sqlite_->saveActiveDays(year, month, days);
}

std::vector<AttendanceRecord> JournalLocal::getMonth(int year, int month)
{
  return sqlite_->getMonth(year, month);
}

bool JournalLocal::saveAttendance(int year, int month,
                                  const std::vector<AttendanceRecord>& data)
{
  return sqlite_->saveAttendance(year, month, data);
}

bool JournalLocal::addParticipantToMonth(int year, int month,
                                         const Participant& participant)
{
  return sqlite_->addParticipantToMonth(year, month, participant);
}

bool JournalLocal::removeParticipantFromMonth(int year, int month,
                                              const ParticipantId& id)
{
  return sqlite_->removeParticipantFromMonth(year, month, id);
}

bool JournalLocal::replaceMonth(int year, int month,
                                const MonthSnapshot& snapshot)
{
  return sqlite_->replaceMonth(year, month, snapshot);
}
