#include "JournalApp.hpp"

#include <QDate>
#include <QDebug>
#include <QSet>
#include <QUuid>

#include <optional>

namespace
{

ParticipantProfile makeParticipantProfile(const QString& fullName)
{
  ParticipantProfile profile;
  profile.id = {QUuid::createUuid().toString(QUuid::WithoutBraces)};
  profile.displayName = fullName;
  profile.fullName = fullName;
  return profile;
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
    : storage_(std::move(storage))
{
  Q_ASSERT(storage_);
}

QString JournalApp::lastError() const
{
  return storage_->lastError();
}

std::optional<std::vector<JournalMonth>> JournalApp::configuredMonths()
{
  return storage_->listMonths();
}

MonthStateResult JournalApp::getMonthState(int year, int month)
{
  return storage_->getMonthState(year, month);
}

MonthSnapshot JournalApp::loadMonth(int year, int month)
{
  MonthSnapshot snapshot = storage_->loadMonthSnapshot(year, month);

  qInfo() << "Month loaded:" << year << month
          << "participants:" << snapshot.participants.size()
          << "active days:" << snapshot.activeDays.size()
          << "records:" << snapshot.attendance.size()
          << "day markers:" << snapshot.dayMarkers.size();
  return snapshot;
}

bool JournalApp::saveMonthSnapshot(int year, int month,
                                   const MonthSnapshot& snapshot)
{
  return snapshot.state == MonthState::Ready &&
         storage_->replaceMonth(year, month, snapshot);
}

bool JournalApp::saveActiveDays(int year, int month, const QVector<int>& days)
{
  return storage_->saveActiveDays(year, month, days);
}

AddParticipantsResult JournalApp::prepareParticipantsFromMonth(
    int fromYear, int fromMonth, int toYear, int toMonth,
    CopyScheduleMode scheduleMode)
{
  if (!QDate(fromYear, fromMonth, 1).isValid() ||
      !QDate(toYear, toMonth, 1).isValid())
  {
    return {false, 0, 0, "Некорректный месяц", {}};
  }
  if (fromYear == toYear && fromMonth == toMonth)
  {
    return {false, 0, 0, "Месяц-источник совпадает с текущим месяцем", {}};
  }

  // Источник и цель читаются целыми aggregates. Несколько отдельных SELECT
  // могли бы смешать состав до чужой записи с attendance после неё.
  const MonthSnapshot sourceSnapshot =
      storage_->loadMonthSnapshot(fromYear, fromMonth);
  if (sourceSnapshot.state == MonthState::Error)
  {
    return {false, 0, 0, sourceSnapshot.errorMessage, {}};
  }
  const MonthSnapshot targetSnapshot =
      storage_->loadMonthSnapshot(toYear, toMonth);
  if (targetSnapshot.state == MonthState::Error)
  {
    return {false, 0, 0, targetSnapshot.errorMessage, {}};
  }

  QVector<int> targetDays;
  if (scheduleMode == CopyScheduleMode::ApplySourceWeekdays)
  {
    if (sourceSnapshot.state == MonthState::Missing)
    {
      // У отсутствующего месяца нет расписания. Сохраняем прежнее поведение:
      // пустой источник создает целевой месяц со всеми календарными днями.
      const auto defaultTargetDays = fullMonthDays(toYear, toMonth);
      if (!defaultTargetDays.has_value())
      {
        return {false, 0, 0, "Некорректный целевой месяц", {}};
      }
      targetDays = *defaultTargetDays;
    }
    else
    {
      const auto mappedDays = mapActiveDaysByWeekday(
          fromYear, fromMonth, sourceSnapshot.activeDays, toYear, toMonth);
      if (!mappedDays.has_value())
      {
        return {false, 0, 0,
                "Некорректная настройка дней недели в месяце-источнике", {}};
      }
      targetDays = *mappedDays;
    }
  }
  else
  {
    targetDays = targetSnapshot.activeDays;
    if (targetDays.isEmpty())
    {
      const auto defaultTargetDays = fullMonthDays(toYear, toMonth);
      if (!defaultTargetDays.has_value())
      {
        return {false, 0, 0, "Некорректный целевой месяц", {}};
      }
      targetDays = *defaultTargetDays;
    }
  }

  const auto activeProfiles = storage_->listParticipantProfiles(false);
  if (!activeProfiles.has_value())
  {
    return {false, 0, 0, "Не удалось прочитать каталог участников", {}};
  }
  QSet<QString> activeProfileIds;
  for (const ParticipantProfile& profile : *activeProfiles)
  {
    activeProfileIds.insert(profile.id.value);
  }
  QSet<QString> targetIds;
  for (const Participant& participant : targetSnapshot.participants)
  {
    targetIds.insert(participant.id.value);
  }

  int copied = 0;
  int skipped = 0;
  std::vector<Participant> targetParticipants = targetSnapshot.participants;
  for (const Participant& participant : sourceSnapshot.participants)
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

  std::vector<AttendanceRecord> mergedAttendance = targetSnapshot.attendance;
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
  snapshot.dayMarkers = targetSnapshot.dayMarkers;
  // Use case только готовит aggregate. UI явно сохраняет его кнопкой, поэтому
  // Cancel/переход на другой месяц не изменяет целевую БД.
  return {true, copied, skipped, QString(), std::move(snapshot)};
}

bool JournalApp::addUser(int year, int month, const QString& fullName)
{
  const QString trimmed = fullName.trimmed();
  if (!QDate(year, month, 1).isValid() || trimmed.isEmpty() ||
      trimmed.size() > kMaxParticipantFullNameLength ||
      trimmed.contains('\n') || trimmed.contains('\r'))
  {
    return false;
  }
  return storage_->addParticipantToMonth(year, month,
                                         makeParticipantProfile(trimmed));
}

bool JournalApp::removeParticipant(int year, int month,
                                   const ParticipantId& id)
{
  if (!QDate(year, month, 1).isValid() || !id.isValid())
  {
    return false;
  }
  return storage_->removeParticipantFromMonth(year, month, id);
}

bool JournalApp::saveAttendance(int year, int month,
                                const std::vector<AttendanceRecord>& data)
{
  return storage_->saveAttendance(year, month, data);
}

bool JournalApp::saveDayMarker(int year, int month,
                               const ParticipantDayMarker& marker)
{
  return storage_->saveDayMarker(year, month, marker);
}

bool JournalApp::removeDayMarker(int year, int month,
                                 const ParticipantId& participantId, int day)
{
  return storage_->removeDayMarker(year, month, participantId, day);
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

std::optional<ParticipantJournalStatistics>
JournalApp::participantStatistics(const ParticipantId& id)
{
  if (!id.isValid())
  {
    return std::nullopt;
  }
  return storage_->participantStatistics(id);
}

std::optional<std::vector<ParticipantProfile>>
JournalApp::participantProfiles(bool includeArchived)
{
  return storage_->listParticipantProfiles(includeArchived);
}

bool JournalApp::updateParticipantProfile(const ParticipantProfile& profile)
{
  ParticipantProfile normalized = profile;
  normalized.historicalName = normalized.historicalName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  normalized.displayName = ParticipantDisplayName(normalized);
  if ((normalized.historicalName.isEmpty() && normalized.fullName.isEmpty()) ||
      !normalized.isValid() ||
      !IsTrainingStartMonthNotAfter(normalized.trainingStartMonth,
                                    QDate::currentDate()))
  {
    return false;
  }
  return storage_->updateParticipantProfile(normalized);
}

bool JournalApp::archiveParticipant(const ParticipantId& id)
{
  return id.isValid() && storage_->setParticipantArchived(id, true);
}

bool JournalApp::restoreParticipant(const ParticipantId& id)
{
  return id.isValid() && storage_->setParticipantArchived(id, false);
}
