#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
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
  return {{QUuid::createUuid().toString(QUuid::WithoutBraces)},
          name,
          name,
          QString()};
}

ParticipantProfile participantProfile(const QString& fullName)
{
  ParticipantProfile profile;
  profile.id = {QUuid::createUuid().toString(QUuid::WithoutBraces)};
  profile.displayName = fullName;
  profile.fullName = fullName;
  return profile;
}

ParticipantEmblem participantEmblem(const ParticipantId& participantId,
                                     const QString& fileName,
                                     qint64 revision = 0)
{
  ParticipantEmblem emblem;
  emblem.participantId = participantId;
  emblem.imageData = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
      "+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  emblem.sha256 = QCryptographicHash::hash(
      emblem.imageData, QCryptographicHash::Sha256);
  emblem.originalFileName = fileName;
  emblem.pixelWidth = 1;
  emblem.pixelHeight = 1;
  emblem.revision = revision;
  return emblem;
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

bool cachedNameWithoutSourceIsRejected(const QString& path)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool rejected = false;
  if (db.open())
  {
    QSqlQuery query(db);
    query.prepare("INSERT INTO participants(id, display_name) "
                  "VALUES(:id, 'Cached only')");
    query.bindValue(":id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    rejected = !query.exec();
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return rejected;
}

bool partialTrainingStartIsRejected(const QString& path,
                                    const ParticipantId& participantId)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool rejected = false;
  if (db.open())
  {
    QSqlQuery query(db);
    query.prepare("UPDATE participants SET training_start_year = 2019, "
                  "training_start_month = NULL WHERE id = :id");
    query.bindValue(":id", participantId.value);
    rejected = !query.exec();
    query.prepare("UPDATE participants SET training_start_year = 1899, "
                  "training_start_month = 12 WHERE id = :id");
    query.bindValue(":id", participantId.value);
    rejected = rejected && !query.exec();
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return rejected;
}

bool strikeIdWithExtraHyphenIsRejected(
    const QString& path, const ParticipantId& participantId)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool rejected = false;
  if (db.open())
  {
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO participant_strike_tests(id, participant_id, "
        "measured_at_utc, hand, strike_count, duration_seconds, weapon, "
        "note, revision) VALUES("
        "'12345678-1234-1234-1234-123456789ab-', :participant_id, "
        "'2026-07-21T18:00:00.000Z', 'right', 1, 1, 'sword', '', 1)");
    query.bindValue(":participant_id", participantId.value);
    rejected = !query.exec();
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return rejected;
}

bool downgradeMeasurementsSchemaToV10(const QString& path)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool downgraded = false;
  if (db.open())
  {
    QSqlQuery query(db);
    downgraded =
        query.exec("DROP TABLE participant_rank_history") &&
        query.exec("ALTER TABLE participants DROP COLUMN club_joined_on") &&
        query.exec("DROP INDEX idx_strike_tests_progress") &&
        query.exec("DROP TABLE participant_strike_tests") &&
        query.exec("DROP TABLE participant_emblems") &&
        query.exec("PRAGMA user_version = 10");
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return downgraded;
}

bool setStoredTrainingStartMonth(const QString& path,
                                 const ParticipantId& participantId,
                                 const JournalMonth& month)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool updated = false;
  if (db.open())
  {
    QSqlQuery query(db);
    query.prepare("UPDATE participants SET training_start_year = :year, "
                  "training_start_month = :month WHERE id = :id");
    query.bindValue(":year", month.year);
    query.bindValue(":month", month.month);
    query.bindValue(":id", participantId.value);
    updated = query.exec() && query.numRowsAffected() == 1;
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return updated;
}

bool isNormalizedSchemaV12(const QString& path, bool expectsTrainerBackup)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool versionOk = false;
  bool hasTrainerColumn = false;
  bool hasHistoricalNameColumn = false;
  bool hasCombatHandColumn = false;
  bool hasTrainingStartYearColumn = false;
  bool hasTrainingStartMonthColumn = false;
  bool hasClubJoinedOnColumn = false;
  bool hasEmblemsTable = false;
  bool hasStrikeTestsTable = false;
  bool hasStrikeProgressIndex = false;
  bool hasRankHistoryTable = false;
  bool backupOk = !expectsTrainerBackup;
  if (db.open())
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 12;
    if (query.exec("PRAGMA table_info(participants)"))
    {
      while (query.next())
      {
        hasTrainerColumn =
            hasTrainerColumn || query.value(1).toString() == "is_trainer";
        hasHistoricalNameColumn =
            hasHistoricalNameColumn ||
            query.value(1).toString() == "historical_name";
        hasCombatHandColumn =
            hasCombatHandColumn || query.value(1).toString() == "combat_hand";
        hasTrainingStartYearColumn =
            hasTrainingStartYearColumn ||
            query.value(1).toString() == "training_start_year";
        hasTrainingStartMonthColumn =
            hasTrainingStartMonthColumn ||
            query.value(1).toString() == "training_start_month";
        hasClubJoinedOnColumn =
            hasClubJoinedOnColumn ||
            query.value(1).toString() == "club_joined_on";
      }
    }
    else
    {
      versionOk = false;
    }
    if (query.exec("SELECT type, name FROM sqlite_master WHERE name IN ("
                   "'participant_emblems', 'participant_strike_tests', "
                   "'idx_strike_tests_progress', "
                   "'participant_rank_history')"))
    {
      while (query.next())
      {
        const QString type = query.value(0).toString();
        const QString name = query.value(1).toString();
        hasEmblemsTable = hasEmblemsTable ||
                          (type == "table" && name == "participant_emblems");
        hasStrikeTestsTable =
            hasStrikeTestsTable ||
            (type == "table" && name == "participant_strike_tests");
        hasStrikeProgressIndex =
            hasStrikeProgressIndex ||
            (type == "index" && name == "idx_strike_tests_progress");
        hasRankHistoryTable =
            hasRankHistoryTable ||
            (type == "table" && name == "participant_rank_history");
      }
    }
    else
    {
      versionOk = false;
    }
    if (expectsTrainerBackup)
    {
      backupOk = query.exec("SELECT count(*) FROM "
                            "legacy_v6_participant_trainer_flags WHERE "
                            "is_trainer = 1") &&
                 query.next() && query.value(0).toInt() == 1;
    }
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return versionOk && !hasTrainerColumn && hasHistoricalNameColumn &&
         hasCombatHandColumn && hasTrainingStartYearColumn &&
         hasTrainingStartMonthColumn && hasClubJoinedOnColumn &&
         hasEmblemsTable && hasStrikeTestsTable && hasStrikeProgressIndex &&
         hasRankHistoryTable && backupOk;
}

bool downgradeParticipantHistorySchemaToV11(const QString& path)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool downgraded = false;
  if (db.open())
  {
    QSqlQuery query(db);
    downgraded =
        query.exec("DROP TABLE participant_rank_history") &&
        query.exec("ALTER TABLE participants DROP COLUMN club_joined_on") &&
        query.exec("PRAGMA user_version = 11");
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return downgraded;
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

bool createSchemaV7Database(const QString& path, const Participant& person,
                            const QString& fullName)
{
  if (!createObsoleteSchemaV6Database(path, person, false))
  {
    return false;
  }
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open() || !db.transaction())
  {
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
    return false;
  }
  bool ok = true;
  {
    QSqlQuery query(db);
    const QStringList statements = {
        "DROP INDEX idx_day_markers_history",
        "ALTER TABLE participant_day_markers RENAME TO "
        "participant_day_markers_v6",
        "CREATE TABLE participant_day_markers(year INTEGER NOT NULL, "
        "month INTEGER NOT NULL, day INTEGER NOT NULL, participant_id TEXT "
        "NOT NULL, kind_mask INTEGER NOT NULL "
        "CHECK(typeof(kind_mask) = 'integer' AND kind_mask BETWEEN 1 AND 31), "
        "note TEXT NOT NULL DEFAULT '' CHECK(length(note) <= 500), "
        "PRIMARY KEY(year, month, day, participant_id), "
        "FOREIGN KEY(year, month, participant_id) REFERENCES "
        "month_participants(year, month, participant_id) ON DELETE CASCADE)",
        "INSERT INTO participant_day_markers SELECT * FROM "
        "participant_day_markers_v6",
        "DROP TABLE participant_day_markers_v6",
        "CREATE INDEX idx_day_markers_history ON participant_day_markers("
        "participant_id, year, month, day)"};
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
      query.prepare("UPDATE participants SET full_name = :full_name, "
                    "contact = '@v7' WHERE id = :id");
      query.bindValue(":full_name", fullName);
      query.bindValue(":id", person.id.value);
      ok = query.exec() && query.numRowsAffected() == 1 &&
           query.exec("PRAGMA user_version = 7");
    }
  }
  ok = ok && db.commit();
  if (!ok)
  {
    db.rollback();
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

bool trainingStartValidationTest()
{
  ParticipantProfile profile = participantProfile("Training Start");
  profile.trainingStartMonth = JournalMonth{1900, 1};
  if (!check(profile.isValid(), "valid training-start month was rejected"))
  {
    return false;
  }
  profile.trainingStartMonth = JournalMonth{1899, 12};
  if (!check(!profile.isValid(), "pre-1900 training-start month was accepted"))
  {
    return false;
  }
  const QDate futureMonth =
      QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1)
          .addMonths(1);
  profile.trainingStartMonth =
      JournalMonth{futureMonth.year(), futureMonth.month()};
  return check(profile.isValid(),
               "structurally valid future month invalidated stored profile") &&
         check(!IsTrainingStartMonthNotAfter(profile.trainingStartMonth,
                                             QDate::currentDate()),
               "future training-start month passed temporal validation");
}

bool freshDatabaseTest(const QString& path)
{
  SqliteConnect storage;
  if (!check(storage.open(path), "fresh database open failed"))
  {
    return false;
  }
  const ParticipantProfile alice = participantProfile("Alice");
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
  const ParticipantDayMarker marker{alice.id, 2,
                                    DayMarkerKind::Payment |
                                        DayMarkerKind::FirstVisit |
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
  if (!check(profile->displayName == "Alice" &&
                 profile->historicalName.isEmpty() &&
                 profile->fullName == "Alice",
             "new participant was not stored as full-name-only"))
  {
    return false;
  }
  profile->historicalName = "Alice Updated";
  profile->fullName = "Alice Example Smith";
  profile->contact = "@alice_example";
  profile->birthday = Birthday{29, 2, std::nullopt};
  profile->trainingStartMonth = JournalMonth{2019, 9};
  profile->joinedClubOn = QDate(2019, 9, 14);
  profile->rankHistory = {
      {ParticipantRank::Recruit, QDate(2019, 10, 1)},
      {ParticipantRank::Novice, std::nullopt},
      {ParticipantRank::Squire, QDate(2021, 5, 16)}};
  profile->rank = ParticipantRank::Squire;
  profile->combatHand = CombatHand::Left;
  profile->notes = "Plain text note";
  if (!check(storage.updateParticipantProfile(*profile),
             "valid profile update failed"))
  {
    return false;
  }
  const auto renamed = storage.getParticipantsForMonth(2025, 7);
  const auto updatedProfile = storage.getParticipantProfile(alice.id);
  const auto historyEntry = [&updatedProfile](ParticipantRank rank)
      -> const ParticipantRankHistoryEntry*
  {
    if (!updatedProfile.has_value())
    {
      return nullptr;
    }
    const auto found = std::find_if(
        updatedProfile->rankHistory.cbegin(),
        updatedProfile->rankHistory.cend(),
        [rank](const ParticipantRankHistoryEntry& entry)
        { return entry.rank == rank; });
    return found == updatedProfile->rankHistory.cend() ? nullptr : &*found;
  };
  const auto* recruitHistory = historyEntry(ParticipantRank::Recruit);
  const auto* noviceHistory = historyEntry(ParticipantRank::Novice);
  const auto* squireHistory = historyEntry(ParticipantRank::Squire);
  if (!check(renamed.size() == 1 && updatedProfile.has_value() &&
                 renamed.front().displayName == "Alice Updated" &&
                 renamed.front().historicalName == "Alice Updated" &&
                 renamed.front().fullName == "Alice Example Smith" &&
                 renamed.front().id == alice.id &&
                 updatedProfile->fullName == "Alice Example Smith" &&
                 updatedProfile->contact == "@alice_example" &&
                 updatedProfile->trainingStartMonth == JournalMonth{2019, 9} &&
                 updatedProfile->joinedClubOn == QDate(2019, 9, 14) &&
                 updatedProfile->rankHistory.size() == 3 &&
                 recruitHistory &&
                 recruitHistory->obtainedOn == QDate(2019, 10, 1) &&
                 noviceHistory && !noviceHistory->obtainedOn.has_value() &&
                 squireHistory &&
                 squireHistory->obtainedOn == QDate(2021, 5, 16) &&
                 updatedProfile->rank == ParticipantRank::Squire &&
                 updatedProfile->combatHand == CombatHand::Left,
             "profile update lost identity, contact, rank, or combat hand"))
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
  invalid.trainingStartMonth = JournalMonth{2020, 13};
  if (!check(!storage.updateParticipantProfile(invalid),
             "invalid training-start month was accepted"))
  {
    return false;
  }
  invalid = *profile;
  const QDate futureMonth =
      QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1)
          .addMonths(1);
  invalid.trainingStartMonth =
      JournalMonth{futureMonth.year(), futureMonth.month()};
  if (!check(!storage.updateParticipantProfile(invalid),
             "future training-start month was accepted"))
  {
    return false;
  }
  const JournalMonth futureStored{futureMonth.year(), futureMonth.month()};
  if (!check(setStoredTrainingStartMonth(path, profile->id, futureStored),
             "test setup could not store a structurally valid future month"))
  {
    return false;
  }
  const auto futureProfile = storage.getParticipantProfile(profile->id);
  if (!check(futureProfile.has_value() &&
                 futureProfile->trainingStartMonth == futureStored,
             "future stored month made the entire profile unreadable") ||
      !check(setStoredTrainingStartMonth(path, profile->id, {2019, 9}),
             "test setup could not restore training-start month"))
  {
    return false;
  }
  invalid = *profile;
  invalid.rankHistory.push_back(
      {ParticipantRank::Recruit, QDate(2020, 1, 1)});
  if (!check(!storage.updateParticipantProfile(invalid),
             "duplicate historical rank was accepted"))
  {
    return false;
  }
  invalid = *profile;
  invalid.rankHistory.push_back({ParticipantRank::Guest, std::nullopt});
  if (!check(!storage.updateParticipantProfile(invalid),
             "guest status was accepted as historical rank"))
  {
    return false;
  }
  invalid = *profile;
  invalid.joinedClubOn = QDate::currentDate().addDays(1);
  if (!check(!storage.updateParticipantProfile(invalid),
             "future club join date was accepted"))
  {
    return false;
  }
  invalid = *profile;
  invalid.rankHistory.front().obtainedOn = QDate::currentDate().addDays(1);
  if (!check(!storage.updateParticipantProfile(invalid),
             "future rank date was accepted"))
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
  invalid.combatHand = static_cast<CombatHand>(999);
  if (!check(!storage.updateParticipantProfile(invalid),
             "unknown combat hand was accepted"))
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
  invalid.historicalName.clear();
  invalid.fullName.clear();
  if (!check(!storage.updateParticipantProfile(invalid),
             "profile without historical or full name was accepted"))
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
  staleSnapshot.participants = {
      {{alice.id.value}, "Stale Alice", QString(), "Stale Alice"}};
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
               "membership removal deleted global profile") &&
         check(partialTrainingStartIsRejected(path, alice.id),
               "database accepted partial training-start month") &&
         check(cachedNameWithoutSourceIsRejected(path),
               "database accepted cached name without a source name");
}

bool participantEmblemTest(const QString& path)
{
  SqliteConnect storage;
  if (!check(storage.open(path), "emblem database open failed"))
  {
    return false;
  }
  const ParticipantProfile alice = participantProfile("Emblem Alice");
  if (!check(storage.addParticipantToMonth(2025, 7, alice),
             "emblem participant insert failed") ||
      !check(!storage.getParticipantEmblem(alice.id).has_value() &&
                 storage.lastError().isEmpty(),
             "missing emblem was reported as a storage error"))
  {
    return false;
  }

  ParticipantProfile createdProfile = alice;
  createdProfile.contact = "@alice";
  const ParticipantEmblem createdEmblem =
      participantEmblem(alice.id, "first.png");
  const ParticipantCardUpdate createUpdate{
      createdProfile, ParticipantEmblemAction::Replace, createdEmblem, 0};
  if (!check(storage.updateParticipantCard(createUpdate),
             "atomic profile and emblem insert failed"))
  {
    return false;
  }
  auto profile = storage.getParticipantProfile(alice.id);
  auto emblem = storage.getParticipantEmblem(alice.id);
  if (!check(profile.has_value() && profile->contact == "@alice",
             "atomic emblem insert lost profile changes") ||
      !check(emblem.has_value() && emblem->revision == 1 &&
                 emblem->originalFileName == "first.png" &&
                 emblem->imageData == createdEmblem.imageData &&
                 emblem->sha256 == createdEmblem.sha256,
             "emblem insert round-trip failed"))
  {
    return false;
  }

  ParticipantProfile replacementProfile = *profile;
  replacementProfile.notes = "second emblem";
  ParticipantEmblem replacementEmblem =
      participantEmblem(alice.id, "second.png", emblem->revision);
  const ParticipantCardUpdate replacement{
      replacementProfile, ParticipantEmblemAction::Replace,
      replacementEmblem, emblem->revision};
  if (!check(storage.updateParticipantCard(replacement),
             "atomic profile and emblem replacement failed"))
  {
    return false;
  }
  profile = storage.getParticipantProfile(alice.id);
  emblem = storage.getParticipantEmblem(alice.id);
  if (!check(profile.has_value() && profile->notes == "second emblem",
             "emblem replacement lost profile changes") ||
      !check(emblem.has_value() && emblem->revision == 2 &&
                 emblem->originalFileName == "second.png",
             "emblem replacement did not increment revision"))
  {
    return false;
  }

  ParticipantProfile staleProfile = *profile;
  staleProfile.notes = "must roll back";
  ParticipantEmblem staleEmblem =
      participantEmblem(alice.id, "stale.png", 1);
  const ParticipantCardUpdate staleReplacement{
      staleProfile, ParticipantEmblemAction::Replace, staleEmblem, 1};
  if (!check(!storage.updateParticipantCard(staleReplacement),
             "stale emblem replacement was accepted") ||
      !check(storage.lastError().contains("revision conflict"),
             "stale emblem replacement did not report a conflict"))
  {
    return false;
  }
  profile = storage.getParticipantProfile(alice.id);
  emblem = storage.getParticipantEmblem(alice.id);
  if (!check(profile.has_value() && profile->notes == "second emblem",
             "stale emblem conflict did not roll profile back") ||
      !check(emblem.has_value() && emblem->revision == 2 &&
                 emblem->originalFileName == "second.png",
             "stale emblem conflict changed stored emblem"))
  {
    return false;
  }

  ParticipantProfile staleRemoveProfile = *profile;
  staleRemoveProfile.notes = "must also roll back";
  const ParticipantCardUpdate staleRemove{
      staleRemoveProfile, ParticipantEmblemAction::Remove, std::nullopt, 1};
  if (!check(!storage.updateParticipantCard(staleRemove),
             "stale emblem removal was accepted"))
  {
    return false;
  }
  profile = storage.getParticipantProfile(alice.id);
  emblem = storage.getParticipantEmblem(alice.id);
  if (!check(profile.has_value() && profile->notes == "second emblem",
             "stale emblem removal did not roll profile back") ||
      !check(emblem.has_value() && emblem->revision == 2,
             "stale emblem removal deleted current emblem"))
  {
    return false;
  }

  ParticipantProfile removedProfile = *profile;
  removedProfile.notes = "emblem removed";
  const ParticipantCardUpdate removeUpdate{
      removedProfile, ParticipantEmblemAction::Remove, std::nullopt,
      emblem->revision};
  if (!check(storage.updateParticipantCard(removeUpdate),
             "atomic profile and emblem removal failed"))
  {
    return false;
  }
  profile = storage.getParticipantProfile(alice.id);
  emblem = storage.getParticipantEmblem(alice.id);
  if (!check(profile.has_value() && profile->notes == "emblem removed",
             "emblem removal lost profile changes") ||
      !check(!emblem.has_value() && storage.lastError().isEmpty(),
             "emblem removal left data or reported a read error"))
  {
    return false;
  }

  const ParticipantId missing{
      QUuid::createUuid().toString(QUuid::WithoutBraces)};
  return check(!storage.saveParticipantEmblem(
                   participantEmblem(missing, "orphan.png")),
               "emblem without participant passed foreign key validation");
}

bool timedStrikeTestsTest(const QString& path)
{
  SqliteConnect storage;
  if (!check(storage.open(path), "timed strikes database open failed"))
  {
    return false;
  }
  const ParticipantProfile alice = participantProfile("Strike Alice");
  const ParticipantProfile bob = participantProfile("Strike Bob");
  if (!check(storage.addParticipantToMonth(2025, 7, alice),
             "first strike participant insert failed") ||
      !check(storage.addParticipantToMonth(2025, 7, bob),
             "second strike participant insert failed") ||
      !check(strikeIdWithExtraHyphenIsRejected(path, alice.id),
             "strike table accepted UUID with an extra hyphen"))
  {
    return false;
  }

  TimedStrikeTest earlier{CreateTimedStrikeTestId(),
                          alice.id,
                          QDateTime::fromString(
                              "2026-07-18T18:00:00.000Z",
                              Qt::ISODateWithMs),
                          StrikeHand::Right,
                          90,
                          30,
                          StrikeWeapon::Sword,
                          "first measurement",
                          0};
  TimedStrikeTest latest{CreateTimedStrikeTestId(),
                         alice.id,
                         QDateTime::fromString(
                             "2026-07-20T18:00:00.000Z",
                             Qt::ISODateWithMs),
                         StrikeHand::Left,
                         60,
                         15,
                         StrikeWeapon::Tyambara,
                         "latest measurement",
                         0};
  TimedStrikeTest bobRecord{CreateTimedStrikeTestId(),
                            bob.id,
                            QDateTime::fromString(
                                "2026-07-19T18:00:00.000Z",
                                Qt::ISODateWithMs),
                            StrikeHand::Right,
                            40,
                            20,
                            StrikeWeapon::Sword,
                            "Bob only",
                            0};
  TimedStrikeTest extremeDate = earlier;
  extremeDate.id = CreateTimedStrikeTestId();
  extremeDate.performedAt =
      QDateTime(QDate(10000, 1, 1), QTime(0, 0), QTimeZone::UTC);
  if (!check(extremeDate.performedAt.isValid() && !extremeDate.isValid() &&
                 !storage.saveTimedStrikeTest(extremeDate),
             "non-canonical extreme timestamp was accepted"))
  {
    return false;
  }
  if (!check(storage.saveTimedStrikeTest(earlier),
             "earlier timed strike insert failed") ||
      !check(storage.saveTimedStrikeTest(latest),
             "latest timed strike insert failed") ||
      !check(storage.saveTimedStrikeTest(bobRecord),
             "second participant timed strike insert failed"))
  {
    return false;
  }

  auto aliceTests = storage.timedStrikeTests(alice.id);
  const auto bobTests = storage.timedStrikeTests(bob.id);
  if (!check(aliceTests.has_value() && aliceTests->size() == 2,
             "timed strike history lost Alice records") ||
      !check(aliceTests->at(0).id == latest.id &&
                 aliceTests->at(1).id == earlier.id,
             "timed strike history is not newest first") ||
      !check(aliceTests->at(0).revision == 1 &&
                 aliceTests->at(0).hand == StrikeHand::Left &&
                 aliceTests->at(0).weapon == StrikeWeapon::Tyambara &&
                 aliceTests->at(0).strikesPerSecond() == 4.0 &&
                 aliceTests->at(0).strikesPerMinute() == 240.0,
             "timed strike round-trip or derived rate failed") ||
      !check(bobTests.has_value() && bobTests->size() == 1 &&
                 bobTests->front().id == bobRecord.id,
             "timed strike histories are not isolated by participant"))
  {
    return false;
  }

  TimedStrikeTest stale = aliceTests->front();
  TimedStrikeTest updated = stale;
  updated.strikeCount = 75;
  updated.note = "corrected";
  if (!check(storage.saveTimedStrikeTest(updated),
             "timed strike update failed"))
  {
    return false;
  }
  aliceTests = storage.timedStrikeTests(alice.id);
  if (!check(aliceTests.has_value() && aliceTests->front().revision == 2 &&
                 aliceTests->front().strikeCount == 75 &&
                 aliceTests->front().note == "corrected" &&
                 aliceTests->front().strikesPerSecond() == 5.0,
             "timed strike update did not round-trip or increment revision"))
  {
    return false;
  }

  stale.strikeCount = 70;
  if (!check(!storage.saveTimedStrikeTest(stale),
             "stale timed strike update was accepted") ||
      !check(storage.lastError().contains("revision conflict"),
             "stale timed strike update did not report a conflict") ||
      !check(!storage.removeTimedStrikeTest(latest.id, 1),
             "stale timed strike removal was accepted"))
  {
    return false;
  }
  aliceTests = storage.timedStrikeTests(alice.id);
  if (!check(aliceTests.has_value() && aliceTests->size() == 2 &&
                 aliceTests->front().revision == 2 &&
                 aliceTests->front().strikeCount == 75,
             "stale timed strike operation changed history") ||
      !check(storage.removeTimedStrikeTest(latest.id, 2),
             "current timed strike removal failed"))
  {
    return false;
  }
  aliceTests = storage.timedStrikeTests(alice.id);
  if (!check(aliceTests.has_value() && aliceTests->size() == 1 &&
                 aliceTests->front().id == earlier.id,
             "timed strike removal deleted wrong record"))
  {
    return false;
  }

  TimedStrikeTest orphan = earlier;
  orphan.id = CreateTimedStrikeTestId();
  orphan.participantId = {
      QUuid::createUuid().toString(QUuid::WithoutBraces)};
  orphan.revision = 0;
  return check(!storage.saveTimedStrikeTest(orphan),
               "timed strike without participant passed foreign key check") &&
         check(!storage.timedStrikeTests({"invalid"}).has_value() &&
                   !storage.lastError().isEmpty(),
               "invalid participant ID returned timed strike history");
}

bool participantStatisticsTest(const QString& path)
{
  SqliteConnect storage;
  if (!check(storage.open(path), "statistics database open failed"))
  {
    return false;
  }
  const ParticipantProfile alice = participantProfile("Statistics Alice");

  if (!check(storage.saveActiveDays(2025, 1, {2, 3, 4, 5}),
             "January active days setup failed") ||
      !check(storage.addParticipantToMonth(2025, 1, alice),
             "January participant insert failed") ||
      !check(storage.saveAttendance(
                 2025, 1,
                 {{alice.id, 2, true}, {alice.id, 4, true},
                  {alice.id, 5, true}}),
             "January attendance setup failed") ||
      !check(storage.saveDayMarker(
                 2025, 1,
                 {alice.id, 2, DayMarkerKind::SpecialTraining, "Special"}),
             "January special marker setup failed") ||
      !check(storage.saveDayMarker(
                 2025, 1,
                 {alice.id, 3, DayMarkerKind::LedTraining, "Led"}),
             "January led marker setup failed") ||
      !check(storage.saveDayMarker(
                 2025, 1,
                 {alice.id, 5,
                  DayMarkerKind::SpecialTraining |
                      DayMarkerKind::LedTraining,
                  "Hidden"}),
             "January hidden marker setup failed") ||
      !check(storage.saveActiveDays(2025, 1, {2, 3, 4}),
             "January active day reduction failed"))
  {
    return false;
  }

  if (!check(storage.saveActiveDays(2025, 2, {1, 2}),
             "February active days setup failed") ||
      !check(storage.addParticipantToMonth(2025, 2, alice),
             "February participant insert failed") ||
      !check(storage.saveActiveDays(2025, 3, {1, 2}),
             "March active days setup failed") ||
      !check(storage.addParticipantToMonth(2025, 3, alice),
             "March participant insert failed") ||
      !check(storage.saveAttendance(
                 2025, 3, {{alice.id, 1, true}, {alice.id, 2, true}}),
             "March attendance setup failed") ||
      !check(storage.saveDayMarker(
                 2025, 3,
                 {alice.id, 1,
                  DayMarkerKind::SpecialTraining |
                      DayMarkerKind::LedTraining,
                  "Special led"}),
             "March special marker setup failed") ||
      !check(storage.saveDayMarker(
                 2025, 3,
                 {alice.id, 2, DayMarkerKind::LedTraining, "Led"}),
             "March led marker setup failed"))
  {
    return false;
  }

  const auto statistics = storage.participantStatistics(alice.id);
  if (!check(statistics.has_value(), "participant statistics missing") ||
      !check(statistics->participantId == alice.id,
             "participant statistics identity mismatch") ||
      !check(statistics->months.size() == 3,
             "participant statistics lost roster month") ||
      !check(statistics->months.at(0).month == JournalMonth{2025, 1} &&
                 statistics->months.at(1).month == JournalMonth{2025, 2} &&
                 statistics->months.at(2).month == JournalMonth{2025, 3},
             "participant statistics are not chronological"))
  {
    return false;
  }
  const ParticipantMonthStatistics& january = statistics->months.at(0);
  const ParticipantMonthStatistics& february = statistics->months.at(1);
  const ParticipantMonthStatistics& march = statistics->months.at(2);
  if (!check(january.trackedDayCount == 3 &&
                 january.attendedDayCount == 2 &&
                 january.specialTrainingVisitCount == 1 &&
                 january.ledTrainingDayCount == 1,
             "January statistics include hidden or unchecked days") ||
      !check(february.trackedDayCount == 2 &&
                 february.attendedDayCount == 0 &&
                 february.specialTrainingVisitCount == 0 &&
                 february.ledTrainingDayCount == 0,
             "zero-attendance roster month statistics are invalid") ||
      !check(march.trackedDayCount == 2 && march.attendedDayCount == 2 &&
                 march.specialTrainingVisitCount == 1 &&
                 march.ledTrainingDayCount == 2,
             "March statistics are invalid") ||
      !check(statistics->totalAttendedDayCount == 4 &&
                 statistics->totalSpecialTrainingVisitCount == 2 &&
                 statistics->totalLedTrainingDayCount == 3,
             "participant statistics totals are invalid") ||
      !check(statistics->firstRecordedVisit == QDate(2025, 1, 2) &&
                 statistics->lastRecordedVisit == QDate(2025, 3, 2),
             "participant visit bounds are invalid"))
  {
    return false;
  }

  if (!check(storage.setParticipantArchived(alice.id, true),
             "statistics participant archive failed"))
  {
    return false;
  }
  const auto archivedStatistics = storage.participantStatistics(alice.id);
  if (!check(archivedStatistics.has_value() &&
                 archivedStatistics->totalAttendedDayCount == 4,
             "archive removed participant statistics"))
  {
    return false;
  }

  const ParticipantId missing{
      QUuid::createUuid().toString(QUuid::WithoutBraces)};
  if (!check(!storage.participantStatistics(missing).has_value() &&
                 !storage.lastError().isEmpty(),
             "missing participant returned zero statistics") ||
      !check(!storage.participantStatistics({"invalid"}).has_value(),
             "invalid participant ID returned statistics"))
  {
    return false;
  }

  if (!check(storage.removeParticipantFromMonth(2025, 1, alice.id),
             "statistics membership removal failed"))
  {
    return false;
  }
  const auto reduced = storage.participantStatistics(alice.id);
  if (!check(reduced.has_value() && reduced->months.size() == 2 &&
                 reduced->totalAttendedDayCount == 2 &&
                 reduced->totalSpecialTrainingVisitCount == 1 &&
                 reduced->totalLedTrainingDayCount == 2 &&
                 reduced->firstRecordedVisit == QDate(2025, 3, 1),
             "membership cascade did not update statistics"))
  {
    return false;
  }

  if (!check(storage.removeParticipantFromMonth(2025, 2, alice.id),
             "February membership removal failed") ||
      !check(storage.removeParticipantFromMonth(2025, 3, alice.id),
             "March membership removal failed"))
  {
    return false;
  }
  const auto empty = storage.participantStatistics(alice.id);
  return check(empty.has_value() && empty->months.empty() &&
                   empty->totalAttendedDayCount == 0 &&
                   !empty->firstRecordedVisit.has_value() &&
                   !empty->lastRecordedVisit.has_value(),
               "profile without memberships returned history");
}

bool snapshotNameSourcesRoundTrip(const QString& sourcePath,
                                  const QString& targetPath)
{
  SqliteConnect source;
  SqliteConnect target;
  if (!check(source.open(sourcePath), "snapshot source open failed") ||
      !check(target.open(targetPath), "snapshot target open failed"))
  {
    return false;
  }

  const QString fullName(kMaxParticipantFullNameLength, QLatin1Char('x'));
  const ParticipantProfile profile = participantProfile(fullName);
  if (!check(source.addParticipantToMonth(2025, 8, profile),
             "long full name was rejected"))
  {
    return false;
  }

  MonthSnapshot snapshot;
  snapshot.state = MonthState::Ready;
  snapshot.participants = source.getParticipantsForMonth(2025, 8);
  snapshot.activeDays = source.getActiveDays(2025, 8);
  snapshot.attendance = source.getMonth(2025, 8);
  snapshot.dayMarkers = source.getDayMarkers(2025, 8);
  if (!check(source.lastError().isEmpty(), "snapshot source read failed") ||
      !check(target.replaceMonth(2025, 8, snapshot),
             "snapshot import into empty database failed"))
  {
    return false;
  }

  const auto imported = target.getParticipantProfile(profile.id);
  return check(imported.has_value() && imported->displayName == fullName &&
                   imported->historicalName.isEmpty() &&
                   imported->fullName == fullName,
               "snapshot import lost ordinary or historical name source");
}

bool migrationV10ToV12Test(const QString& path)
{
  ParticipantProfile before = participantProfile("V10 Alice");
  before.contact = "@v10";
  before.rank = ParticipantRank::Squire;
  before.combatHand = CombatHand::Left;
  before.trainingStartMonth = JournalMonth{2019, 9};
  {
    SqliteConnect storage;
    if (!check(storage.open(path), "v10 migration setup open failed") ||
        !check(storage.addParticipantToMonth(2025, 7, before),
               "v10 migration participant setup failed"))
    {
      return false;
    }
  }
  if (!check(downgradeMeasurementsSchemaToV10(path),
             "v10 migration schema setup failed"))
  {
    return false;
  }

  SqliteConnect storage;
  if (!check(storage.open(path), "schema v10 to v12 migration failed"))
  {
    return false;
  }
  const auto after = storage.getParticipantProfile(before.id);
  const auto roster = storage.getParticipantsForMonth(2025, 7);
  return check(after.has_value() && after->fullName == "V10 Alice" &&
                   after->contact == "@v10" &&
                   after->rank == ParticipantRank::Squire &&
                   after->combatHand == CombatHand::Left &&
                   after->trainingStartMonth == JournalMonth{2019, 9},
               "v10 to v12 migration changed participant profile") &&
         check(roster.size() == 1 && roster.front().id == before.id,
               "v10 to v12 migration lost month membership") &&
         check(isNormalizedSchemaV12(path, false),
               "v10 migration did not produce clean schema v12");
}

bool migrationV11ToV12Test(const QString& path)
{
  const ParticipantProfile before = participantProfile("V11 Alice");
  {
    SqliteConnect storage;
    if (!check(storage.open(path), "v11 migration setup open failed") ||
        !check(storage.addParticipantToMonth(2025, 7, before),
               "v11 migration participant setup failed"))
    {
      return false;
    }
  }
  if (!check(downgradeParticipantHistorySchemaToV11(path),
             "v11 migration schema setup failed"))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "schema v11 to v12 migration failed"))
  {
    return false;
  }
  const auto after = storage.getParticipantProfile(before.id);
  return check(after.has_value() && !after->joinedClubOn.has_value() &&
                   after->rankHistory.empty(),
               "v11 migration invented participant history") &&
         check(isNormalizedSchemaV12(path, false),
               "v11 migration did not produce clean schema v12");
}

bool migrationV5ToV12Test(const QString& path)
{
  const Participant alice = participant("V5 Alice");
  if (!createSchemaV5Database(path, alice))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "schema v5 to v12 migration failed"))
  {
    return false;
  }
  const auto profile = storage.getParticipantProfile(alice.id);
  const auto migratedMarkers = storage.getDayMarkers(2025, 7);
  const ParticipantDayMarker trainerMarker{
      alice.id, 2, DayMarkerKind::LedTraining, "Вёл тренировку"};
  return check(profile.has_value() &&
                   profile->rank == ParticipantRank::Knight &&
                   profile->combatHand == CombatHand::Unknown &&
                   !profile->trainingStartMonth.has_value() &&
                   profile->historicalName == "V5 Alice" &&
                   profile->fullName.isEmpty() && profile->contact.isEmpty(),
               "v5 migration lost rank or assigned invalid detail defaults") &&
         check(migratedMarkers.size() == 1 &&
                   migratedMarkers.front().kinds == DayMarkerKind::Payment,
               "v5 migration lost old day marker") &&
         check(storage.saveDayMarker(2025, 7, trainerMarker),
               "v12 schema rejected trainer day marker") &&
         check(storage.open(path), "migrated v12 reopen is not idempotent") &&
         check(isNormalizedSchemaV12(path, false),
               "v5 migration did not produce clean schema v12");
}

bool migrationV7ToV12Test(const QString& path)
{
  const Participant alice = participant("V7 Alice");
  const QString fullName(kMaxParticipantFullNameLength, QLatin1Char('f'));
  if (!createSchemaV7Database(path, alice, fullName))
  {
    return false;
  }
  SqliteConnect storage;
  if (!check(storage.open(path), "schema v7 to v12 migration failed"))
  {
    return false;
  }
  auto profile = storage.getParticipantProfile(alice.id);
  if (!check(profile.has_value() && profile->displayName == "V7 Alice" &&
                 profile->historicalName == "V7 Alice" &&
                 profile->fullName == fullName &&
                 !profile->trainingStartMonth.has_value(),
             "v7 migration confused historical and ordinary names"))
  {
    return false;
  }
  profile->historicalName.clear();
  return check(storage.updateParticipantProfile(*profile),
               "migrated schema rejected 300-character ordinary name") &&
         check(storage.getParticipantProfile(alice.id)->displayName == fullName,
               "ordinary-name fallback failed after v7 migration") &&
         check(isNormalizedSchemaV12(path, false),
               "v7 migration did not produce clean schema v12");
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
  const ParticipantDayMarker trainerMarker{
      alice.id, 2, DayMarkerKind::LedTraining, "Вёл тренировку"};
  return check(storage.saveDayMarker(2025, 7, trainerMarker),
               "repaired schema v6 rejected trainer marker") &&
         check(storage.open(path),
               "repaired development schema v6 reopen failed") &&
      check(isNormalizedSchemaV12(path, withTrainerColumn),
            "development schema v6 was not normalized to v12");
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
  const bool ok =
      birthdayValidationTest() && trainingStartValidationTest() &&
      freshDatabaseTest(directory.filePath("fresh.db")) &&
      participantEmblemTest(directory.filePath("emblem.db")) &&
      timedStrikeTestsTest(directory.filePath("timed-strikes.db")) &&
      participantStatisticsTest(directory.filePath("statistics.db")) &&
      snapshotNameSourcesRoundTrip(directory.filePath("snapshot-source.db"),
                                   directory.filePath("snapshot-target.db")) &&
      migrationV10ToV12Test(directory.filePath("v10.db")) &&
      migrationV11ToV12Test(directory.filePath("v11.db")) &&
      migrationV5ToV12Test(directory.filePath("v5.db")) &&
      migrationV7ToV12Test(directory.filePath("v7.db")) &&
      developmentV6IsRepaired(directory.filePath("old-v6-trainer.db"), true) &&
      developmentV6IsRepaired(directory.filePath("old-v6-marker.db"), false);
  return ok ? 0 : 1;
}
