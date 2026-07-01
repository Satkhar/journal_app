#include "JournalApp.hpp"

#include <QDebug>
#include <QElapsedTimer>

JournalApp::JournalApp(std::unique_ptr<IJournalStorage> storage,
                       bool allowBootstrapWrites)
    : storage_(std::move(storage)),
      allowBootstrapWrites_(allowBootstrapWrites),
      currentYear_(0),
      currentMonth_(0) {}

//---------------------------------------------------------------

MonthSnapshot JournalApp::loadMonth(int year, int month) {
  QElapsedTimer totalTimer;
  totalTimer.start();

  // Запоминаем контекст текущего месяца для addUser/deleteUser.
  currentYear_ = year;
  currentMonth_ = month;

  MonthSnapshot snapshot;
  QElapsedTimer readTimer;
  readTimer.start();
  // users, activeDays и attendance читаются отдельно, чтобы UI отрисовал строки и отметки.
  snapshot.users = storage_->getUsersForMonth(year, month);
  snapshot.activeDays = storage_->getActiveDays(year, month);
  snapshot.attendance = storage_->getMonth(year, month);
  qInfo() << "loadMonth read stage ms:" << readTimer.elapsed();

  // Сохраняем MVP-поведение для local: при пустом месяце создаем Alice.
  // Для remote режима запись при чтении отключена.
  if (allowBootstrapWrites_ && snapshot.users.isEmpty()) {
    qInfo() << "Month is empty, creating default user Alice";
    if (storage_->addUser(year, month, "Alice")) {
      snapshot.users = storage_->getUsersForMonth(year, month);
      snapshot.activeDays = storage_->getActiveDays(year, month);
      snapshot.attendance = storage_->getMonth(year, month);

      // Как и раньше: стартовое заполнение шахматкой по дням.
      for (AttendanceRecord& record : snapshot.attendance) {
        record.isChecked = (record.day % 2) != 0;
      }
      storage_->saveMonth(year, month, snapshot.attendance);
      snapshot.attendance = storage_->getMonth(year, month);
    }
  }

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
