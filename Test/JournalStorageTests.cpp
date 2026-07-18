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

bool invalidMarkerMaskIsRejected(const QString& path,
                                 const ParticipantId& participantId)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool rejected = false;
  if (db.open())
  {
    QSqlQuery query(db);
    query.prepare("INSERT INTO participant_day_markers(year, month, day, "
                  "participant_id, kind_mask, note) "
                  "VALUES(2025, 7, 3, :id, 32, '')");
    query.bindValue(":id", participantId.value);
    rejected = !query.exec();
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return rejected;
}

bool isNormalizedSchemaV7(const QString& path, bool expectsTrainerBackup)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool versionOk = false;
  bool hasTrainerColumn = false;
  bool backupOk = !expectsTrainerBackup;
  if (db.open())
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 7;
    if (query.exec("PRAGMA table_info(participants)"))
    {
      while (query.next())
      {
        hasTrainerColumn =
            hasTrainerColumn || query.value(1).toString() == "is_trainer";
      }
    }
    else
    {
      versionOk = false;
    }
    if (expectsTrainerBackup)
    {
      backupOk =
          query.exec("SELECT count(*) FROM "
                     "legacy_v6_participant_trainer_flags WHERE "
                     "is_trainer = 1") &&
          query.next() && query.value(0).toInt() == 1;
    }
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return versionOk && !hasTrainerColumn && backupOk;
}

bool createSchemaV5Database(const QString& path, const Participant& person)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool ok = true;
  {
    QSqlQuery query(db);
    const QStringList statements = {
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE participants(id TEXT PRIMARY KEY NOT NULL, "
        "display_name TEXT NOT NULL, birth_day INTEGER, birth_month INTEGER, "
        "birth_year INTEGER, notes TEXT NOT NULL DEFAULT '', created_at TEXT "
        "NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT "
        "CURRENT_TIMESTAMP, archived_at TEXT, rank TEXT NOT NULL DEFAULT "
        "'guest' CHECK(rank IN ('page', 'squire', 'novice', 'recruit', "
        "'guest', 'knight')))",
        "CREATE TABLE month_participants(year INTEGER NOT NULL, month INTEGER "
        "NOT NULL, participant_id TEXT NOT NULL, sort_order INTEGER NOT NULL, "
        "PRIMARY KEY(year, month, participant_id), FOREIGN KEY(participant_id) "
        "REFERENCES participants(id))",
        "CREATE TABLE attendance(year INTEGER NOT NULL, month INTEGER NOT "
        "NULL, day INTEGER NOT NULL, participant_id TEXT NOT NULL, is_checked "
        "INTEGER NOT NULL, PRIMARY KEY(year, month, day, participant_id), "
        "FOREIGN KEY(year, month, participant_id) REFERENCES "
        "month_participants(year, month, participant_id) ON DELETE CASCADE)",
        "CREATE TABLE month_days(year INTEGER NOT NULL, month INTEGER NOT "
        "NULL, day INTEGER NOT NULL, PRIMARY KEY(year, month, day))",
        "CREATE TABLE participant_day_markers(year INTEGER NOT NULL, month "
        "INTEGER NOT NULL, day INTEGER NOT NULL, participant_id TEXT NOT NULL, "
        "kind_mask INTEGER NOT NULL CHECK(kind_mask BETWEEN 1 AND 15), note "
        "TEXT NOT NULL DEFAULT '', PRIMARY KEY(year, month, day, "
        "participant_id), FOREIGN KEY(year, month, participant_id) REFERENCES "
        "month_participants(year, month, participant_id) ON DELETE CASCADE)",
        "CREATE INDEX idx_day_markers_history ON participant_day_markers("
        "participant_id, year, month, day)",
        "CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
        "participants BEGIN SELECT 1; END",
        "CREATE TRIGGER participants_profile_update BEFORE UPDATE ON "
        "participants BEGIN SELECT 1; END"};
    for (const QString& statement : statements)
    {
      if (!query.exec(statement))
      {
        ok = false;
        break;
      }
    }
    if (ok)
    {
      query.prepare("INSERT INTO participants(id, display_name, rank) "
                    "VALUES(:id, :name, 'knight')");
      query.bindValue(":id", person.id.value);
      query.bindValue(":name", person.displayName);
      ok = query.exec();
    }
    if (ok)
    {
      query.prepare("INSERT INTO month_participants "
                    "VALUES(2025, 7, :id, 0)");
      query.bindValue(":id", person.id.value);
      ok = query.exec();
    }
    if (ok)
    {
      query.prepare("INSERT INTO participant_day_markers(year, month, day, "
                    "participant_id, kind_mask, note) VALUES(2025, 7, 1, "
                    ":id, 1, 'v5 marker')");
      query.bindValue(":id", person.id.value);
      ok = query.exec();
    }
    ok = ok && query.exec("PRAGMA user_version = 5");
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return ok;
}

bool createObsoleteSchemaV6Database(const QString& path,
                                    const Participant& person,
                                    bool withTrainerColumn)
{
  if (!createSchemaV5Database(path, person))
  {
    return false;
  }
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool ok = true;
  {
    QSqlQuery query(db);
    ok = query.exec("ALTER TABLE participants ADD COLUMN full_name TEXT NOT "
                    "NULL DEFAULT '' CHECK(length(full_name) <= 300 AND "
                    "instr(full_name, char(10)) = 0 AND "
                    "instr(full_name, char(13)) = 0)") &&
         query.exec("ALTER TABLE participants ADD COLUMN contact TEXT NOT "
                    "NULL DEFAULT '' CHECK(length(contact) <= 500 AND "
                    "instr(contact, char(10)) = 0 AND "
                    "instr(contact, char(13)) = 0)");
    if (ok && withTrainerColumn)
    {
      ok = query.exec("ALTER TABLE participants ADD COLUMN is_trainer "
                      "INTEGER NOT NULL DEFAULT 0 CHECK(typeof(is_trainer) = "
                      "'integer' AND is_trainer IN (0, 1))") &&
           query.exec("UPDATE participants SET is_trainer = 1");
    }
    ok = ok && query.exec("PRAGMA user_version = 6");
  }
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
  const ParticipantDayMarker marker{
      alice.id, 2,
      DayMarkerKind::Payment | DayMarkerKind::FirstVisit |
          DayMarkerKind::LedTraining,
      "First paid visit"};
  if (!check(storage.saveDayMarker(2025, 7, marker), "day marker save failed"))
  {
    return false;
  }
  auto markers = storage.getDayMarkers(2025, 7);
  if (!check(markers.size() == 1 && markers.front().day == 2 &&
                 markers.front().kinds == marker.kinds &&
                 markers.front().note == marker.note,
             "day marker round-trip failed"))
  {
    return false;
  }
  ParticipantDayMarker updatedMarker = marker;
  updatedMarker.kinds = DayMarkerKind::Other;
  updatedMarker.note = "Updated";
  if (!check(storage.saveDayMarker(2025, 7, updatedMarker),
             "day marker update failed"))
  {
    return false;
  }
  markers = storage.getDayMarkers(2025, 7);
  if (!check(markers.size() == 1 &&
                 markers.front().kinds == updatedMarker.kinds &&
                 markers.front().note == updatedMarker.note,
             "day marker upsert did not replace values") ||
      !check(storage.removeDayMarker(2025, 7, alice.id, 2),
             "day marker removal failed") ||
      !check(storage.getDayMarkers(2025, 7).empty(),
             "day marker removal left a row") ||
      !check(!storage.saveDayMarker(2025, 7,
                                    {alice.id, 2, DayMarkerKinds(), "Invalid"}),
             "empty day marker kind was accepted") ||
      !check(invalidMarkerMaskIsRejected(path, alice.id),
             "day marker table accepted unknown kind bits") ||
      !check(storage.saveDayMarker(2025, 7, marker),
             "day marker restore failed"))
  {
    return false;
  }

  auto profile = storage.getParticipantProfile(alice.id);
  if (!check(profile.has_value(), "participant profile missing"))
  {
    return false;
  }
  profile->displayName = "Alice Updated";
  profile->fullName = "Alice Example Smith";
  profile->contact = "@alice_example";
  profile->birthday = Birthday{29, 2, std::nullopt};
  profile->rank = ParticipantRank::Squire;
  profile->notes = "Plain text note";
  if (!check(storage.updateParticipantProfile(*profile),
             "valid profile update failed"))
  {
    return false;
  }
  const auto renamed = storage.getParticipantsForMonth(2025, 7);
  const auto updatedProfile = storage.getParticipantProfile(alice.id);
  if (!check(renamed.size() == 1 && updatedProfile.has_value() &&
                 renamed.front().displayName == "Alice Updated" &&
                 renamed.front().id == alice.id &&
                 updatedProfile->fullName == "Alice Example Smith" &&
                 updatedProfile->contact == "@alice_example" &&
                 updatedProfile->rank == ParticipantRank::Squire,
             "profile update lost identity, contact, or rank"))
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
  invalid = *profile;
  invalid.rank = static_cast<ParticipantRank>(999);
  if (!check(!storage.updateParticipantProfile(invalid),
             "unknown participant rank was accepted"))
  {
    return false;
  }
  invalid = *profile;
  invalid.fullName = QString(kMaxParticipantFullNameLength + 1, 'x');
  if (!check(!storage.updateParticipantProfile(invalid),
             "oversized full name was accepted"))
  {
    return false;
  }
  invalid = *profile;
  invalid.contact = "@alice\nsecond-line";
  if (!check(!storage.updateParticipantProfile(invalid),
             "multiline contact was accepted"))
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
  staleSnapshot.dayMarkers = {marker};
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

bool migrationV5ToV7Test(const QString& path)
{
  const Participant alice = participant("V5 Alice");
  if (!createSchemaV5Database(path, alice))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "schema v5 to v7 migration failed"))
  {
    return false;
  }
  const auto profile = storage.getParticipantProfile(alice.id);
  const auto migratedMarkers = storage.getDayMarkers(2025, 7);
  const ParticipantDayMarker trainerMarker{alice.id, 2,
                                           DayMarkerKind::LedTraining,
                                           "Вёл тренировку"};
  return check(profile.has_value() &&
                   profile->rank == ParticipantRank::Knight &&
                   profile->fullName.isEmpty() && profile->contact.isEmpty(),
               "v5 migration lost rank or assigned invalid detail defaults") &&
         check(migratedMarkers.size() == 1 &&
                   migratedMarkers.front().kinds == DayMarkerKind::Payment,
               "v5 migration lost old day marker") &&
         check(storage.saveDayMarker(2025, 7, trainerMarker),
               "v7 schema rejected trainer day marker") &&
         check(storage.open(path), "migrated v7 reopen is not idempotent") &&
         check(isNormalizedSchemaV7(path, false),
               "v5 migration did not produce clean schema v7");
}

bool developmentV6IsRepaired(const QString& path, bool withTrainerColumn)
{
  const Participant alice = participant("Obsolete V6 Alice");
  if (!createObsoleteSchemaV6Database(path, alice, withTrainerColumn))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "development schema v6 repair failed"))
  {
    return false;
  }
  const ParticipantDayMarker trainerMarker{alice.id, 2,
                                           DayMarkerKind::LedTraining,
                                           "Вёл тренировку"};
  return check(storage.saveDayMarker(2025, 7, trainerMarker),
               "repaired schema v6 rejected trainer marker") &&
         check(storage.open(path),
               "repaired development schema v6 reopen failed") &&
         check(isNormalizedSchemaV7(path, withTrainerColumn),
               "development schema v6 was not normalized to v7");
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
                  migrationV5ToV7Test(directory.filePath("v5.db")) &&
                  developmentV6IsRepaired(
                      directory.filePath("old-v6-trainer.db"), true) &&
                  developmentV6IsRepaired(
                      directory.filePath("old-v6-marker.db"), false);
  return ok ? 0 : 1;
}
