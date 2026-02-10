#include "JournalLocal.hpp"

JournalLocal::JournalLocal(std::unique_ptr<SqliteConnect> sqlite)
    : sqlite_(std::move(sqlite)) {}

//---------------------------------------------------------------

QStringList JournalLocal::getUsersForMonth(int year, int month) {
  // Адаптер доменного интерфейса к конкретной реализации SQLite.
  return sqlite_->getUsersForMonth(year, month);
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> JournalLocal::getMonth(int year, int month) {
  return sqlite_->getMonth(year, month);
}

//---------------------------------------------------------------

bool JournalLocal::saveMonth(int year, int month,
                             const std::vector<AttendanceRecord>& data) {
  return sqlite_->saveMonth(year, month, data);
}

//---------------------------------------------------------------

bool JournalLocal::addUser(int year, int month, const QString& name) {
  return sqlite_->addUser(year, month, name);
}

//---------------------------------------------------------------

bool JournalLocal::deleteUser(int year, int month, const QString& name) {
  return sqlite_->deleteUser(year, month, name);
}

//---------------------------------------------------------------
