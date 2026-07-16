#include "JournalApp.hpp"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>

#include <optional>

namespace
{

Participant makeParticipant(const QString& displayName)
{
  return {{QUuid::createUuid().toString(QUuid::WithoutBraces)}, displayName};
}

std::optional<QVector<int>> fullMonthDays(int year, int month)
{
  const QDate firstDay(year, month, 1);
  if (!firstDay.isValid())
  {
    return std::nullopt;
  }

  QVector<int> days;
  days.reserve(firstDay.daysInMonth());
  for (int day = 1; day <= firstDay.daysInMonth(); ++day)
  {
    days.push_back(day);
  }
  return days;
}

std::optional<QVector<int>> mapActiveDaysByWeekday(
    int fromYear, int fromMonth, const QVector<int>& sourceDays, int toYear,
    int toMonth)
{
  const QDate sourceMonth(fromYear, fromMonth, 1);
  const QDate targetMonth(toYear, toMonth, 1);
  if (!sourceMonth.isValid() || !targetMonth.isValid() || sourceDays.isEmpty())
  {
    return std::nullopt;
  }

  QSet<int> weekdays;
  for (int day : sourceDays)
  {
    const QDate sourceDate(fromYear, fromMonth, day);
    if (!sourceDate.isValid())
    {
      return std::nullopt;
    }
    weekdays.insert(sourceDate.dayOfWeek());
  }

  QVector<int> targetDays;
  for (int day = 1; day <= targetMonth.daysInMonth(); ++day)
  {
    if (weekdays.contains(QDate(toYear, toMonth, day).dayOfWeek()))
    {
      targetDays.push_back(day);
    }
  }
  return targetDays;
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
                                               CopyScheduleMode scheduleMode)
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
  QVector<int> targetDays;
  if (scheduleMode == CopyScheduleMode::ApplySourceWeekdays)
  {
    if (sourceState.state == MonthState::Missing)
    {
      // У отсутствующего месяца нет расписания. Сохраняем прежнее поведение:
      // пустой источник создает целевой месяц со всеми календарными днями.
      const auto defaultTargetDays = fullMonthDays(toYear, toMonth);
      if (!defaultTargetDays.has_value())
      {
        return {false, 0, 0, "Некорректный целевой месяц"};
      }
      targetDays = *defaultTargetDays;
    }
    else
    {
      const QVector<int> sourceDays =
          storage_->getActiveDays(fromYear, fromMonth);
      if (!storage_->lastError().isEmpty())
      {
        return {false, 0, 0, storage_->lastError()};
      }
      const auto mappedDays = mapActiveDaysByWeekday(
          fromYear, fromMonth, sourceDays, toYear, toMonth);
      if (!mappedDays.has_value())
      {
        return {false, 0, 0,
                "Некорректная настройка дней недели в месяце-источнике"};
      }
      targetDays = *mappedDays;
    }
  }
  else
  {
    targetDays = storage_->getActiveDays(toYear, toMonth);
    if (!storage_->lastError().isEmpty())
    {
      return {false, 0, 0, storage_->lastError()};
    }
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
