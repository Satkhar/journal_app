#include "JournalLocal.hpp"

JournalLocal::JournalLocal(std::unique_ptr<SqliteConnect> sqlite)
    : sqlite_(std::move(sqlite))
{
}

QString JournalLocal::lastError() const
{
  return sqlite_->lastError();
}

std::optional<std::vector<JournalMonth>> JournalLocal::listMonths()
{
  return sqlite_->listMonths();
}

MonthSnapshot JournalLocal::loadMonthSnapshot(int year, int month)
{
  return sqlite_->loadMonthSnapshot(year, month);
}

MonthStateResult JournalLocal::getMonthState(int year, int month)
{
  return sqlite_->getMonthState(year, month);
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

std::vector<ParticipantDayMarker> JournalLocal::getDayMarkers(int year,
                                                              int month)
{
  return sqlite_->getDayMarkers(year, month);
}

bool JournalLocal::saveDayMarker(int year, int month,
                                 const ParticipantDayMarker& marker)
{
  return sqlite_->saveDayMarker(year, month, marker);
}

bool JournalLocal::removeDayMarker(int year, int month,
                                   const ParticipantId& participantId,
                                   int day)
{
  return sqlite_->removeDayMarker(year, month, participantId, day);
}

bool JournalLocal::addParticipantToMonth(int year, int month,
                                         const ParticipantProfile& profile)
{
  return sqlite_->addParticipantToMonth(year, month, profile);
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

std::optional<ParticipantProfile>
JournalLocal::getParticipantProfile(const ParticipantId& id)
{
  return sqlite_->getParticipantProfile(id);
}

std::optional<ParticipantJournalStatistics>
JournalLocal::participantStatistics(const ParticipantId& id)
{
  return sqlite_->participantStatistics(id);
}

std::optional<std::vector<ParticipantProfile>>
JournalLocal::listParticipantProfiles(bool includeArchived)
{
  return sqlite_->listParticipantProfiles(includeArchived);
}

bool JournalLocal::updateParticipantProfile(const ParticipantProfile& profile)
{
  return sqlite_->updateParticipantProfile(profile);
}

bool JournalLocal::setParticipantArchived(const ParticipantId& id,
                                          bool archived)
{
  return sqlite_->setParticipantArchived(id, archived);
}
