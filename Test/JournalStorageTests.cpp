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

bool createOldDevelopmentDatabase(const QString& path)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  QSqlQuery query(db);
  const bool ok = query.exec(
      "CREATE TABLE users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT "
      "NULL, date TEXT NOT NULL, is_checked INTEGER NOT NULL)");
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return ok;
}

bool createSchemaV2Database(const QString& path, const Participant& person)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  QSqlQuery query(db);
  const QStringList statements = {
      "PRAGMA foreign_keys = ON",
      "CREATE TABLE participants(id TEXT PRIMARY KEY NOT NULL, display_name "
      "TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
      "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, archived_at TEXT)",
      "CREATE TABLE month_participants(year INTEGER NOT NULL, month INTEGER "
      "NOT NULL, participant_id TEXT NOT NULL, sort_order INTEGER NOT NULL, "
      "PRIMARY KEY(year, month, participant_id), FOREIGN KEY(participant_id) "
      "REFERENCES participants(id))",
      "CREATE TABLE attendance(year INTEGER NOT NULL, month INTEGER NOT NULL, "
      "day INTEGER NOT NULL, participant_id TEXT NOT NULL, is_checked INTEGER "
      "NOT NULL, PRIMARY KEY(year, month, day, participant_id), FOREIGN KEY("
      "year, month, participant_id) REFERENCES month_participants(year, "
      "month, participant_id) ON DELETE CASCADE)",
      "CREATE TABLE month_days(year INTEGER NOT NULL, month INTEGER NOT NULL, "
      "day INTEGER NOT NULL, PRIMARY KEY(year, month, day))"};
  bool ok = true;
  for (const QString& statement : statements)
  {
    ok = ok && query.exec(statement);
  }
  query.prepare(
      "INSERT INTO participants(id, display_name) VALUES(:id, :name)");
  query.bindValue(":id", person.id.value);
  query.bindValue(":name", person.displayName);
  ok = ok && query.exec();
  query.prepare("INSERT INTO month_participants VALUES(2025, 7, :id, 0)");
  query.bindValue(":id", person.id.value);
  ok = ok && query.exec();
  ok = ok && query.exec("INSERT INTO month_days VALUES(2025, 7, 1)");
  query.prepare("INSERT INTO attendance VALUES(2025, 7, 1, :id, 1)");
  query.bindValue(":id", person.id.value);
  ok = ok && query.exec();
  ok = ok && query.exec("PRAGMA user_version = 2");
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return ok;
}

bool birthdayValidationTest()
{
  return check(Birthday{29, 2, std::nullopt}.isValid(),
               "29 February without year was rejected") &&
         check(Birthday{29, 2, 2024}.isValid(),
               "valid leap birthday was rejected") &&
         check(!Birthday{29, 2, 2023}.isValid(),
               "invalid leap birthday was accepted") &&
         check(!Birthday{31, 4, std::nullopt}.isValid(),
               "invalid month day was accepted");
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
             "attendance save failed"))
  {
    return false;
  }

  auto profile = storage.getParticipantProfile(alice.id);
  if (!check(profile.has_value(), "participant profile missing"))
  {
    return false;
  }
  profile->displayName = "Alice Updated";
  profile->birthday = Birthday{29, 2, std::nullopt};
  profile->notes = "Plain text note";
  if (!check(storage.updateParticipantProfile(*profile),
             "valid profile update failed"))
  {
    return false;
  }
  const auto renamed = storage.getParticipantsForMonth(2025, 7);
  if (!check(renamed.size() == 1 &&
                 renamed.front().displayName == "Alice Updated" &&
                 renamed.front().id == alice.id,
             "rename changed ID or failed to update roster"))
  {
    return false;
  }

  ParticipantProfile invalid = *profile;
  invalid.birthday = Birthday{29, 2, 2023};
  if (!check(!storage.updateParticipantProfile(invalid),
             "invalid birthday was accepted"))
  {
    return false;
  }
  invalid = *profile;
  invalid.notes = QString(4097, 'x');
  if (!check(!storage.updateParticipantProfile(invalid),
             "oversized notes were accepted"))
  {
    return false;
  }

  if (!check(storage.setParticipantArchived(alice.id, true), "archive failed"))
  {
    return false;
  }
  const auto archivedRoster = storage.getParticipantsForMonth(2025, 7);
  if (!check(archivedRoster.size() == 1,
             "archived participant disappeared from month"))
  {
    return false;
  }
  attendance = storage.getMonth(2025, 7);
  dayTwo = std::find_if(attendance.begin(), attendance.end(),
                        [](const AttendanceRecord& record)
                        { return record.day == 2; });
  if (!check(dayTwo != attendance.end() && dayTwo->isChecked,
             "archive deleted attendance"))
  {
    return false;
  }
  if (!check(storage.listParticipantProfiles(false)->empty(),
             "archive filter returned archived participant") ||
      !check(storage.listParticipantProfiles(true)->size() == 1,
             "directory lost archived participant") ||
      !check(storage.setParticipantArchived(alice.id, false), "restore failed"))
  {
    return false;
  }

  MonthSnapshot staleSnapshot;
  staleSnapshot.participants = {{{alice.id.value}, "Stale Alice"}};
  staleSnapshot.activeDays = {1, 2};
  staleSnapshot.attendance = {{alice.id, 1, false}, {alice.id, 2, true}};
  if (!check(storage.replaceMonth(2025, 7, staleSnapshot),
             "month replacement failed") ||
      !check(storage.getParticipantProfile(alice.id)->displayName ==
                 "Alice Updated",
             "month snapshot overwrote global profile"))
  {
    return false;
  }
  return check(storage.removeParticipantFromMonth(2025, 7, alice.id),
               "membership removal failed") &&
         check(storage.getParticipantProfile(alice.id).has_value(),
               "membership removal deleted global profile");
}

bool migrationV2ToV3Test(const QString& path)
{
  const Participant alice = participant("Migrated Alice");
  if (!createSchemaV2Database(path, alice))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "schema v2 to v3 migration failed"))
  {
    return false;
  }
  const auto profile = storage.getParticipantProfile(alice.id);
  const auto attendance = storage.getMonth(2025, 7);
  return check(profile.has_value() && profile->notes.isEmpty() &&
                   !profile->birthday.has_value(),
               "migration produced invalid profile defaults") &&
         check(attendance.size() == 1 && attendance.front().isChecked,
               "migration lost attendance") &&
         check(storage.open(path), "schema v3 reopen is not idempotent");
}

bool migrationRollbackTest(const QString& path)
{
  const Participant invalid = participant("   ");
  if (!createSchemaV2Database(path, invalid))
  {
    return false;
  }
  {
    SqliteConnect storage;
    if (!check(!storage.open(path), "invalid schema v2 migration succeeded"))
    {
      return false;
    }
  }

  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  QSqlQuery query(db);
  bool ok = query.exec("PRAGMA user_version") && query.next() &&
            query.value(0).toInt() == 2;
  bool hasBirthDay = false;
  if (query.exec("PRAGMA table_info(participants)"))
  {
    while (query.next())
    {
      hasBirthDay = hasBirthDay || query.value(1).toString() == "birth_day";
    }
  }
  else
  {
    ok = false;
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return check(ok && !hasBirthDay,
               "failed migration did not roll back schema and version");
}
bool unsupportedDatabaseTest(const QString& path)
{
  if (!createOldDevelopmentDatabase(path))
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
  const bool ok = birthdayValidationTest() &&
                  freshDatabaseTest(directory.filePath("fresh.db")) &&
                  migrationV2ToV3Test(directory.filePath("v2.db")) &&
                  migrationRollbackTest(directory.filePath("invalid-v2.db")) &&
                  unsupportedDatabaseTest(directory.filePath("old.db"));
  return ok ? 0 : 1;
}
