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

JournalApp::JournalApp(std::unique_ptr<IJournalStorage> storage,
                       bool allowBootstrapWrites)
    : storage_(std::move(storage)), allowBootstrapWrites_(allowBootstrapWrites),
      currentYear_(0), currentMonth_(0)
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

  if (allowBootstrapWrites_ && snapshot.participants.empty())
  {
    const Participant alice = makeParticipant("Alice");
    if (storage_->addParticipantToMonth(year, month, alice))
    {
      snapshot.participants = storage_->getParticipantsForMonth(year, month);
      snapshot.attendance = storage_->getMonth(year, month);
      for (AttendanceRecord& record : snapshot.attendance)
      {
        record.isChecked = (record.day % 2) != 0;
      }
      storage_->saveAttendance(year, month, snapshot.attendance);
      snapshot.attendance = storage_->getMonth(year, month);
    }
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
  QSet<QString> targetIds;
  for (const Participant& participant : target)
  {
    targetIds.insert(participant.id.value);
  }

  int copied = 0;
  int skipped = 0;
  for (const Participant& participant : source)
  {
    if (targetIds.contains(participant.id.value))
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
  if (currentYear_ == 0 || currentMonth_ == 0 || trimmed.isEmpty())
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
