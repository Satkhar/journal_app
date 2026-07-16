#include "JournalApp.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>

JournalApp::JournalApp(std::unique_ptr<IJournalStorage> storage)
    : storage_(std::move(storage)),
      currentYear_(0),
      currentMonth_(0) {}

//---------------------------------------------------------------

MonthStateResult JournalApp::getMonthState(int year, int month) {
  return storage_->getMonthState(year, month);
}

//---------------------------------------------------------------

MonthSnapshot JournalApp::loadMonth(int year, int month) {
  QElapsedTimer totalTimer;
  totalTimer.start();

  // Запоминаем контекст текущего месяца для addUser/deleteUser.
  currentYear_ = year;
  currentMonth_ = month;

  MonthSnapshot snapshot;
  const MonthStateResult state = storage_->getMonthState(year, month);
  snapshot.state = state.state;
  snapshot.errorMessage = state.errorMessage;
  if (state.state == MonthState::Error) {
    qWarning() << "loadMonth state read failed:" << state.errorMessage;
    return snapshot;
  }

  QElapsedTimer readTimer;
  readTimer.start();
  // users, activeDays и attendance читаются отдельно, чтобы UI отрисовал строки и отметки.
  snapshot.users = storage_->getUsersForMonth(year, month);
  if (!storage_->lastError().isEmpty()) {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }
  snapshot.activeDays = storage_->getActiveDays(year, month);
  if (!storage_->lastError().isEmpty()) {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }
  snapshot.attendance = storage_->getMonth(year, month);
  if (!storage_->lastError().isEmpty()) {
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = storage_->lastError();
    return snapshot;
  }
  qInfo() << "loadMonth read stage ms:" << readTimer.elapsed();

  qInfo() << "Month loaded:" << year << month
          << "users:" << snapshot.users.size()
          << "active days:" << snapshot.activeDays.size()
          << "records:" << snapshot.attendance.size()
          << "total ms:" << totalTimer.elapsed();
  return snapshot;
}

//---------------------------------------------------------------

bool JournalApp::saveActiveDays(int year, int month, const QVector<int>& days) {
  QElapsedTimer timer;
  timer.start();

  currentYear_ = year;
  currentMonth_ = month;
  const bool ok = storage_->saveActiveDays(year, month, days);
  qInfo() << "saveActiveDays:" << year << month << "days:" << days.size()
          << "result:" << ok << "ms:" << timer.elapsed();
  return ok;
}

//---------------------------------------------------------------

CopyUsersResult JournalApp::copyUsersFromMonth(int fromYear, int fromMonth,
                                               int toYear, int toMonth,
                                               bool copyActiveDays) {
  QElapsedTimer timer;
  timer.start();

  if (fromYear == toYear && fromMonth == toMonth) {
    return {false, 0, 0, "Месяц-источник совпадает с текущим месяцем"};
  }

  const MonthStateResult sourceState =
      storage_->getMonthState(fromYear, fromMonth);
  if (sourceState.state == MonthState::Error) {
    return {false, 0, 0, sourceState.errorMessage};
  }
  const MonthStateResult targetState = storage_->getMonthState(toYear, toMonth);
  if (targetState.state == MonthState::Error) {
    return {false, 0, 0, targetState.errorMessage};
  }

  const QStringList sourceUsers = storage_->getUsersForMonth(fromYear, fromMonth);
  if (!storage_->lastError().isEmpty()) {
    return {false, 0, 0, storage_->lastError()};
  }
  QStringList targetUsers = storage_->getUsersForMonth(toYear, toMonth);
  if (!storage_->lastError().isEmpty()) {
    return {false, 0, 0, storage_->lastError()};
  }
  const std::vector<AttendanceRecord> targetAttendance =
      storage_->getMonth(toYear, toMonth);
  if (!storage_->lastError().isEmpty()) {
    return {false, 0, 0, storage_->lastError()};
  }

  const QVector<int> targetDays = copyActiveDays
                                      ? storage_->getActiveDays(fromYear, fromMonth)
                                      : storage_->getActiveDays(toYear, toMonth);
  if (!storage_->lastError().isEmpty()) {
    return {false, 0, 0, storage_->lastError()};
  }

  QHash<QString, QHash<int, bool>> existingMarks;
  for (const AttendanceRecord& record : targetAttendance) {
    existingMarks[record.userName][record.day] = record.isChecked;
  }

  int copied = 0;
  int skipped = 0;
  for (const QString& user : sourceUsers) {
    if (targetUsers.contains(user)) {
      ++skipped;
      continue;
    }

    targetUsers.push_back(user);
    ++copied;
  }

  std::vector<AttendanceRecord> targetData;
  targetData.reserve(static_cast<std::size_t>(targetUsers.size()) *
                     static_cast<std::size_t>(targetDays.size()));
  for (const QString& user : targetUsers) {
    for (int day : targetDays) {
      bool isChecked = false;
      const auto userMarks = existingMarks.constFind(user);
      if (userMarks != existingMarks.cend()) {
        isChecked = userMarks->value(day, false);
      }
      targetData.push_back({user, day, isChecked});
    }
  }

  if (!storage_->saveMonthSetup(toYear, toMonth, targetDays, targetData)) {
    const QString error = storage_->lastError();
    return {false, 0, 0,
            error.isEmpty() ? "Не удалось атомарно создать месяц" : error};
  }

  currentYear_ = toYear;
  currentMonth_ = toMonth;
  qInfo() << "copyUsersFromMonth:" << fromYear << fromMonth << "->" << toYear
          << toMonth << "copied:" << copied << "skipped:" << skipped
          << "copy days:" << copyActiveDays << "ms:" << timer.elapsed();
  return {true, copied, skipped, QString()};
}

//---------------------------------------------------------------

bool JournalApp::addUser(const QString& name) {
  QElapsedTimer timer;
  timer.start();

  // Защита от вызова "в никуда": UI обязан сначала открыть месяц через loadMonth().
  if (currentYear_ == 0 || currentMonth_ == 0) {
    qWarning() << "addUser called without selected month";
    return false;
  }
  const bool ok = storage_->addUser(currentYear_, currentMonth_, name);
  qInfo() << "addUser:" << name << "result:" << ok << "ms:" << timer.elapsed();
  return ok;
}

//---------------------------------------------------------------

bool JournalApp::deleteUser(const QString& name) {
  QElapsedTimer timer;
  timer.start();

  // deleteUser использует тот же запомненный контекст месяца, что и addUser.
  if (currentYear_ == 0 || currentMonth_ == 0) {
    qWarning() << "deleteUser called without selected month";
    return false;
  }
  const bool ok = storage_->deleteUser(currentYear_, currentMonth_, name);
  qInfo() << "deleteUser:" << name << "result:" << ok << "ms:" << timer.elapsed();
  return ok;
}

//---------------------------------------------------------------

bool JournalApp::saveMonth(int year, int month,
                           const std::vector<AttendanceRecord>& data) {
  QElapsedTimer timer;
  timer.start();

  // После saveMonth этот месяц становится "текущим" для последующих add/delete.
  currentYear_ = year;
  currentMonth_ = month;
  const bool ok = storage_->saveMonth(year, month, data);
  qInfo() << "saveMonth:" << year << month << "rows:" << data.size() << "result:" << ok
          << "ms:" << timer.elapsed();
  return ok;
}

//---------------------------------------------------------------
