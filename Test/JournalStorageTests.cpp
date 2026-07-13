#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

#include "SqliteConnect.hpp"

namespace
{

bool check(bool condition, const char* message)
{
  if (!condition)
  {
    qCritical() << message;
  }
  return condition;
}

Participant participant(const QString& name)
{
  return {{QUuid::createUuid().toString(QUuid::WithoutBraces)}, name};
}

bool createOldDevelopmentDatabase(const QString& path,
                                  const QList<QStringList>& rows)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  QSqlQuery query(db);
  bool ok = query.exec(
      "CREATE TABLE users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT "
      "NULL, date TEXT NOT NULL, is_checked INTEGER NOT NULL)");
  for (const QStringList& row : rows)
  {
    query.prepare(
        "INSERT INTO users(name, date, is_checked) VALUES(:name, :date, "
        ":checked)");
    query.bindValue(":name", row.at(0));
    query.bindValue(":date", row.at(1));
    query.bindValue(":checked", row.at(2).toInt());
    ok = ok && query.exec();
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return ok;
}

bool freshDatabaseTest(const QString& path)
{
  SqliteConnect storage;
  if (!check(storage.open(path), "fresh database open failed"))
  {
    return false;
  }
  const Participant alice = participant("Alice");
  if (!check(storage.addParticipantToMonth(2025, 7, alice),
             "participant insert failed"))
  {
    return false;
  }
  const auto july = storage.getParticipantsForMonth(2025, 7);
  if (!check(july.size() == 1 && july.front().id == alice.id,
             "participant identity was not preserved"))
  {
    return false;
  }
  if (!check(storage.getParticipantsForMonth(2026, 7).empty(),
             "different years overlap"))
  {
    return false;
  }

  auto attendance = storage.getMonth(2025, 7);
  auto dayTwo = std::find_if(attendance.begin(), attendance.end(),
                             [](const AttendanceRecord& record)
                             { return record.day == 2; });
  if (!check(dayTwo != attendance.end(), "day 2 attendance missing"))
  {
    return false;
  }
  dayTwo->isChecked = true;
  if (!check(storage.saveAttendance(2025, 7, {*dayTwo}),
             "attendance save failed") ||
      !check(storage.saveActiveDays(2025, 7, {1}),
             "active day shrink failed") ||
      !check(storage.saveActiveDays(2025, 7, {1, 2}),
             "active day expand failed"))
  {
    return false;
  }
  attendance = storage.getMonth(2025, 7);
  dayTwo = std::find_if(attendance.begin(), attendance.end(),
                        [](const AttendanceRecord& record)
                        { return record.day == 2; });
  if (!check(dayTwo != attendance.end() && dayTwo->isChecked,
             "inactive day history was deleted"))
  {
    return false;
  }
  const Participant invalid{{"not-a-uuid"}, "Invalid"};
  if (!check(!storage.addParticipantToMonth(2025, 7, invalid),
             "non-canonical participant ID was accepted"))
  {
    return false;
  }
  MonthSnapshot staleSnapshot;
  staleSnapshot.participants = {{{alice.id.value}, "Stale Alice"}};
  staleSnapshot.activeDays = {1};
  staleSnapshot.attendance = {{alice.id, 1, false}};
  if (!check(storage.replaceMonth(2025, 7, staleSnapshot),
             "month replacement failed") ||
      !check(storage.getParticipantsForMonth(2025, 7).front().displayName ==
                 "Alice",
             "month snapshot overwrote global profile"))
  {
    return false;
  }
  if (!check(storage.removeParticipantFromMonth(2025, 7, alice.id),
             "membership removal failed") ||
      !check(storage.getParticipantsForMonth(2025, 7).empty(),
             "membership still exists"))
  {
    return false;
  }
  return check(storage.addParticipantToMonth(2026, 7, alice),
               "global participant was deleted with membership");
}

bool unsupportedDatabaseTest(const QString& path)
{
  if (!createOldDevelopmentDatabase(path, {{"Alice", "01.07", "1"}}))
  {
    return false;
  }
  SqliteConnect storage;
  return check(!storage.open(path),
               "old unversioned development database was accepted");
}
} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!check(directory.isValid(), "temporary directory creation failed"))
  {
    return 1;
  }
  const bool ok = freshDatabaseTest(directory.filePath("fresh.db")) &&
                  unsupportedDatabaseTest(directory.filePath("old.db"));
  return ok ? 0 : 1;
}
