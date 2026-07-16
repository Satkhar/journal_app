#include "JournalApp.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>

namespace
{

Participant makeParticipant(const QString& displayName)
{
  return {{QUuid::createUuid().toString(QUuid::WithoutBraces)}, displayName};
}

} // namespace

JournalApp::JournalApp(std::unique_ptr<IJournalStorage> storage)
    : storage_(std::move(storage)), currentYear_(0), currentMonth_(0)
{
}

MonthStateResult JournalApp::getMonthState(int year, int month)
{
  return storage_->getMonthState(year, month);
}

MonthSnapshot JournalApp::loadMonth(int year, int month)
{
  QElapsedTimer timer;
  timer.start();
  currentYear_ = year;
  currentMonth_ = month;

  MonthSnapshot snapshot;
  const MonthStateResult state = storage_->getMonthState(year, month);
  snapshot.state = state.state;
  snapshot.errorMessage = state.errorMessage;
  if (state.state == MonthState::Error)
  {
    return snapshot;
  }
  snapshot.participants = storage_->getParticipantsForMonth(year, month);
  if (!storage_->lastError().isEmpty())
  {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }
  snapshot.activeDays = storage_->getActiveDays(year, month);
  if (!storage_->lastError().isEmpty())
  {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }
  snapshot.attendance = storage_->getMonth(year, month);
  if (!storage_->lastError().isEmpty())
  {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }

  qInfo() << "Month loaded:" << year << month
          << "participants:" << snapshot.participants.size()
          << "active days:" << snapshot.activeDays.size()
          << "records:" << snapshot.attendance.size()
          << "ms:" << timer.elapsed();
  return snapshot;
}

bool JournalApp::saveActiveDays(int year, int month, const QVector<int>& days)
{
  currentYear_ = year;
  currentMonth_ = month;
  return storage_->saveActiveDays(year, month, days);
}

CopyUsersResult JournalApp::copyUsersFromMonth(int fromYear, int fromMonth,
                                               int toYear, int toMonth,
                                               bool copyActiveDays)
{
  if (fromYear == toYear && fromMonth == toMonth)
  {
    return {false, 0, 0, "Месяц-источник совпадает с текущим месяцем"};
  }

  const MonthStateResult sourceState =
      storage_->getMonthState(fromYear, fromMonth);
  if (sourceState.state == MonthState::Error)
  {
    return {false, 0, 0, sourceState.errorMessage};
  }

  const auto source = storage_->getParticipantsForMonth(fromYear, fromMonth);
  if (!storage_->lastError().isEmpty())
  {
    return {false, 0, 0, storage_->lastError()};
  }
  const auto target = storage_->getParticipantsForMonth(toYear, toMonth);
  if (!storage_->lastError().isEmpty())
  {
    return {false, 0, 0, storage_->lastError()};
  }
  QVector<int> targetDays = storage_->getActiveDays(
      copyActiveDays ? fromYear : toYear, copyActiveDays ? fromMonth : toMonth);
  if (!storage_->lastError().isEmpty())
  {
    return {false, 0, 0, storage_->lastError()};
  }
  const auto targetAttendance = storage_->getMonth(toYear, toMonth);
  if (!storage_->lastError().isEmpty())
  {
    return {false, 0, 0, storage_->lastError()};
  }
  const auto activeProfiles = storage_->listParticipantProfiles(false);
  if (!activeProfiles.has_value())
  {
    return {false, 0, 0, "Не удалось прочитать каталог участников"};
  }
  QSet<QString> activeProfileIds;
  for (const ParticipantProfile& profile : *activeProfiles)
  {
    activeProfileIds.insert(profile.id.value);
  }
  QSet<QString> targetIds;
  for (const Participant& participant : target)
  {
    targetIds.insert(participant.id.value);
  }

  int copied = 0;
  int skipped = 0;
  std::vector<Participant> targetParticipants = target;
  for (const Participant& participant : source)
  {
    if (!activeProfileIds.contains(participant.id.value) ||
        targetIds.contains(participant.id.value))
    {
      ++skipped;
      continue;
    }
    targetParticipants.push_back(participant);
    targetIds.insert(participant.id.value);
    ++copied;
  }

  std::vector<AttendanceRecord> mergedAttendance = targetAttendance;
  QSet<QString> attendanceKeys;
  for (const AttendanceRecord& record : mergedAttendance)
  {
    attendanceKeys.insert(record.participantId.value + ':' +
                          QString::number(record.day));
  }
  for (const Participant& participant : targetParticipants)
  {
    for (int day : targetDays)
    {
      const QString key = participant.id.value + ':' + QString::number(day);
      if (!attendanceKeys.contains(key))
      {
        mergedAttendance.push_back({participant.id, day, false});
        attendanceKeys.insert(key);
      }
    }
  }

  MonthSnapshot snapshot;
  snapshot.state = MonthState::Ready;
  snapshot.participants = std::move(targetParticipants);
  snapshot.activeDays = std::move(targetDays);
  snapshot.attendance = std::move(mergedAttendance);
  if (!storage_->replaceMonth(toYear, toMonth, snapshot))
  {
    const QString error = storage_->lastError();
    return {false, 0, 0,
            error.isEmpty() ? "Не удалось атомарно создать месяц" : error};
  }

  currentYear_ = toYear;
  currentMonth_ = toMonth;
  return {true, copied, skipped, QString()};
}

bool JournalApp::addUser(const QString& name)
{
  const QString trimmed = name.trimmed();
  if (currentYear_ == 0 || currentMonth_ == 0 || trimmed.isEmpty() ||
      trimmed.size() > 200)
  {
    return false;
  }
  return storage_->addParticipantToMonth(currentYear_, currentMonth_,
                                         makeParticipant(trimmed));
}

bool JournalApp::removeParticipant(const ParticipantId& id)
{
  if (currentYear_ == 0 || currentMonth_ == 0 || !id.isValid())
  {
    return false;
  }
  return storage_->removeParticipantFromMonth(currentYear_, currentMonth_, id);
}

bool JournalApp::saveAttendance(int year, int month,
                                const std::vector<AttendanceRecord>& data)
{
  currentYear_ = year;
  currentMonth_ = month;
  return storage_->saveAttendance(year, month, data);
}

std::optional<ParticipantProfile>
JournalApp::participantProfile(const ParticipantId& id)
{
  if (!id.isValid())
  {
    return std::nullopt;
  }
  return storage_->getParticipantProfile(id);
}

std::optional<std::vector<ParticipantProfile>>
JournalApp::participantProfiles(bool includeArchived)
{
  return storage_->listParticipantProfiles(includeArchived);
}

bool JournalApp::updateParticipantProfile(const ParticipantProfile& profile)
{
  if (!profile.isValid())
  {
    return false;
  }
  return storage_->updateParticipantProfile(profile);
}

bool JournalApp::archiveParticipant(const ParticipantId& id)
{
  return id.isValid() && storage_->setParticipantArchived(id, true);
}

bool JournalApp::restoreParticipant(const ParticipantId& id)
{
  return id.isValid() && storage_->setParticipantArchived(id, false);
}
