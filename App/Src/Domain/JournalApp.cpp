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

MonthSnapshot JournalApp::loadMonth(int year, int month)
{
  QElapsedTimer timer;
  timer.start();
  currentYear_ = year;
  currentMonth_ = month;

  MonthSnapshot snapshot;
  snapshot.participants = storage_->getParticipantsForMonth(year, month);
  snapshot.activeDays = storage_->getActiveDays(year, month);
  snapshot.attendance = storage_->getMonth(year, month);

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

  const auto source = storage_->getParticipantsForMonth(fromYear, fromMonth);
  if (source.empty())
  {
    return {true, 0, 0, QString()};
  }

  if (copyActiveDays &&
      !storage_->saveActiveDays(toYear, toMonth,
                                storage_->getActiveDays(fromYear, fromMonth)))
  {
    return {false, 0, 0, "Не удалось перенести дни учета"};
  }

  const auto target = storage_->getParticipantsForMonth(toYear, toMonth);
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
  for (const Participant& participant : source)
  {
    if (!activeProfileIds.contains(participant.id.value) ||
        targetIds.contains(participant.id.value))
    {
      ++skipped;
      continue;
    }
    if (!storage_->addParticipantToMonth(toYear, toMonth, participant))
    {
      return {false, copied, skipped,
              QString("Не удалось перенести участника: %1")
                  .arg(participant.displayName)};
    }
    targetIds.insert(participant.id.value);
    ++copied;
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
