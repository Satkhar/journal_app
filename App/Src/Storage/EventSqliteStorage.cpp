#include "EventSqliteStorage.hpp"

#include <QDebug>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <limits>
#include <utility>

namespace
{

constexpr int kEventSchemaVersionV1 = 1;
constexpr int kEventSchemaV1ParticipantSnapshotNameMaxLength = 200;
constexpr int kEventSchemaVersionV2 = 2;
constexpr int kEventSchemaVersionV3 = 3;
constexpr int kEventSchemaVersionV4 = 4;
constexpr int kEventSchemaVersion = 5;

bool TableHasColumns(QSqlDatabase& db, const QString& table,
                     const QSet<QString>& required)
{
  QSet<QString> actual;
  QSqlQuery query(db);
  if (!query.exec(QString("PRAGMA table_info(%1)").arg(table)))
  {
    return false;
  }
  while (query.next())
  {
    actual.insert(query.value(1).toString());
  }
  return actual.contains(required);
}

EventRecord NormalizeEvent(const EventRecord& source)
{
  EventRecord result = source;
  result.title = result.title.trimmed();
  if (result.notes.isNull())
  {
    result.notes = QStringLiteral("");
  }
  for (EventParticipantSnapshot& participant : result.participants)
  {
    participant.displayNameSnapshot = participant.displayNameSnapshot.trimmed();
    participant.fullNameSnapshot = participant.fullNameSnapshot.trimmed();
    if (participant.fullNameSnapshot.isNull())
    {
      participant.fullNameSnapshot = QStringLiteral("");
    }
  }
  for (EventParticipantSnapshot& attendee : result.nonCompetingAttendees)
  {
    attendee.displayNameSnapshot = attendee.displayNameSnapshot.trimmed();
    attendee.fullNameSnapshot = attendee.fullNameSnapshot.trimmed();
    if (attendee.fullNameSnapshot.isNull())
    {
      attendee.fullNameSnapshot = QStringLiteral("");
    }
  }
  for (EventBout& bout : result.bouts)
  {
    bout.sideA.freeName = bout.sideA.freeName.trimmed();
    bout.sideB.freeName = bout.sideB.freeName.trimmed();
  }
  return result;
}

QString CreateEventParticipantsTableSql(
    const QString& tableName, int participantSnapshotNameMaxLength)
{
  return QString(
             "CREATE TABLE %1("
             "event_id TEXT NOT NULL, participant_id TEXT NOT NULL, "
             "participant_name_snapshot TEXT NOT NULL "
             "CHECK(length(trim(participant_name_snapshot)) BETWEEN 1 AND "
             "%2 AND instr(participant_name_snapshot, char(10)) = 0 AND "
             "instr(participant_name_snapshot, char(13)) = 0), "
             "participant_full_name_snapshot TEXT NOT NULL DEFAULT '' "
             "CHECK(length(participant_full_name_snapshot) <= %3 AND "
             "instr(participant_full_name_snapshot, char(10)) = 0 AND "
             "instr(participant_full_name_snapshot, char(13)) = 0), "
             "sort_order INTEGER NOT NULL CHECK(sort_order >= 0), "
             "PRIMARY KEY(event_id, participant_id), "
             "UNIQUE(event_id, sort_order), "
             "FOREIGN KEY(event_id) REFERENCES events(id) ON DELETE "
             "CASCADE)")
      .arg(tableName, QString::number(participantSnapshotNameMaxLength),
           QString::number(kMaxParticipantFullNameLength));
}

QString CreateEventsTableSql(const QString& tableName, int maxCategory)
{
  return QString(
             "CREATE TABLE %1("
             "id TEXT PRIMARY KEY NOT NULL, "
             "title TEXT NOT NULL CHECK(length(trim(title)) BETWEEN 1 AND 200 "
             "AND instr(title, char(10)) = 0 AND "
             "instr(title, char(13)) = 0), "
             "event_date TEXT NOT NULL CHECK(length(event_date) = 10 AND "
             "date(event_date) IS NOT NULL AND date(event_date) = event_date "
             "AND CAST(substr(event_date, 1, 4) AS INTEGER) BETWEEN 1 AND "
             "9999), "
             "event_category INTEGER NOT NULL DEFAULT 0 "
             "CHECK(typeof(event_category) = 'integer' AND "
             "event_category BETWEEN 0 AND %2), "
             "revision INTEGER NOT NULL DEFAULT 1 "
             "CHECK(typeof(revision) = 'integer' AND revision >= 1), "
             "notes TEXT NOT NULL DEFAULT '' CHECK(length(notes) <= 32768), "
             "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
             "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)")
      .arg(tableName, QString::number(maxCategory));
}

} // namespace

EventSqliteStorage::EventSqliteStorage() = default;

EventSqliteStorage::~EventSqliteStorage()
{
  if (db_.isOpen())
  {
    db_.close();
  }
  if (!connectionName_.isEmpty())
  {
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
  }
}

bool EventSqliteStorage::open(const QString& path)
{
  lastError_.clear();
  if (!connectionName_.isEmpty())
  {
    setError("Event database connection is already initialized");
    return false;
  }
  if (path.trimmed().isEmpty())
  {
    setError("Event database path is empty");
    return false;
  }
  connectionName_ =
      QString("event_connection_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
  db_.setDatabaseName(path);
  if (!db_.open())
  {
    setError(db_.lastError().text());
    return false;
  }
  return enableForeignKeys() && ensureSchema();
}

QString EventSqliteStorage::lastError() const
{
  return lastError_;
}

void EventSqliteStorage::setError(const QString& error)
{
  lastError_ = error;
  qWarning() << "EventSqliteStorage:" << error;
}

bool EventSqliteStorage::enableForeignKeys()
{
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA busy_timeout = 5000") ||
      !query.exec("PRAGMA foreign_keys = ON") ||
      !query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 1)
  {
    setError("SQLite foreign key enforcement is unavailable for event DB");
    return false;
  }
  return true;
}

bool EventSqliteStorage::tableExists(const QString& name) const
{
  QSqlQuery query(db_);
  query.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' "
                "AND name = :name");
  query.bindValue(":name", name);
  return query.exec() && query.next();
}

bool EventSqliteStorage::ensureSchema()
{
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA user_version") || !query.next())
  {
    setError("Cannot read event schema version");
    return false;
  }
  const int version = query.value(0).toInt();
  query.finish();
  if (version == kEventSchemaVersion)
  {
    return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                        3, true);
  }
  if (version == kEventSchemaVersionV4)
  {
    if (!verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                      2, false) ||
        !migrateSchemaV4ToV5())
    {
      return false;
    }
    return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                        3, true);
  }
  if (version == kEventSchemaVersionV3)
  {
    if (!verifySchema(kMaxEventParticipantSnapshotNameLength, true, false,
                      0, false) ||
        !migrateSchemaV3ToV4() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                      2, false) ||
        !migrateSchemaV4ToV5())
    {
      return false;
    }
    return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                        3, true);
  }
  if (version == kEventSchemaVersionV2)
  {
    if (!verifySchema(kMaxEventParticipantSnapshotNameLength, false, false,
                      0, false) ||
        !migrateSchemaV2ToV3() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, true, false,
                      0, false) ||
        !migrateSchemaV3ToV4() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                      2, false) ||
        !migrateSchemaV4ToV5())
    {
      return false;
    }
    return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                        3, true);
  }
  if (version == kEventSchemaVersionV1)
  {
    if (!verifySchema(kEventSchemaV1ParticipantSnapshotNameMaxLength,
                       false, false, 0, false) ||
        !migrateSchemaV1ToV2() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, false, false,
                      0, false) ||
        !migrateSchemaV2ToV3() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, true, false,
                      0, false) ||
        !migrateSchemaV3ToV4() ||
        !verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                      2, false) ||
        !migrateSchemaV4ToV5())
    {
      return false;
    }
    return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                        3, true);
  }
  if (version != 0)
  {
    setError(QString("Unsupported event schema version: %1").arg(version));
    return false;
  }
  if (tableExists("events") || tableExists("event_participants") ||
      tableExists("event_attendees") || tableExists("event_bouts"))
  {
    setError("Partial unversioned event schema detected");
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  const QString versionSql =
      QString("PRAGMA user_version = %1").arg(kEventSchemaVersion);
  if (!createSchema() || !query.exec(versionSql) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot create event schema");
    }
    return false;
  }
  query.finish();
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return verifySchema(kMaxEventParticipantSnapshotNameLength, true, true,
                      3, true);
}

bool EventSqliteStorage::createSchema()
{
  const QStringList statements = {
      CreateEventsTableSql("events", 3),
      CreateEventParticipantsTableSql(
          "event_participants", kMaxEventParticipantSnapshotNameLength),
      "CREATE INDEX idx_event_participants_history ON "
      "event_participants(participant_id, event_id)",
      CreateEventParticipantsTableSql(
          "event_attendees", kMaxEventParticipantSnapshotNameLength),
      "CREATE INDEX idx_event_attendees_history ON "
      "event_attendees(participant_id, event_id)",
      "CREATE TABLE event_bouts("
      "id TEXT PRIMARY KEY NOT NULL, event_id TEXT NOT NULL, "
      "sort_order INTEGER NOT NULL CHECK(sort_order >= 0), "
      "side_a_participant_id TEXT, side_a_free_name TEXT, "
      "side_b_participant_id TEXT, side_b_free_name TEXT, "
      "score_a INTEGER NOT NULL CHECK(typeof(score_a) = 'integer' AND "
      "score_a BETWEEN 0 AND 2147483647), "
      "score_b INTEGER NOT NULL CHECK(typeof(score_b) = 'integer' AND "
      "score_b BETWEEN 0 AND 2147483647), "
      "UNIQUE(event_id, sort_order), "
      "FOREIGN KEY(event_id) REFERENCES events(id) ON DELETE CASCADE, "
      "FOREIGN KEY(event_id, side_a_participant_id) REFERENCES "
      "event_participants(event_id, participant_id), "
      "FOREIGN KEY(event_id, side_b_participant_id) REFERENCES "
      "event_participants(event_id, participant_id), "
      "CHECK((side_a_participant_id IS NOT NULL AND side_a_free_name IS NULL) "
      "OR (side_a_participant_id IS NULL AND side_a_free_name IS NOT NULL AND "
      "length(trim(side_a_free_name)) BETWEEN 1 AND 200 AND "
      "instr(side_a_free_name, char(10)) = 0 AND "
      "instr(side_a_free_name, char(13)) = 0)), "
      "CHECK((side_b_participant_id IS NOT NULL AND side_b_free_name IS NULL) "
      "OR (side_b_participant_id IS NULL AND side_b_free_name IS NOT NULL AND "
      "length(trim(side_b_free_name)) BETWEEN 1 AND 200 AND "
      "instr(side_b_free_name, char(10)) = 0 AND "
      "instr(side_b_free_name, char(13)) = 0)), "
      "CHECK(side_a_participant_id IS NULL OR side_b_participant_id IS NULL "
      "OR side_a_participant_id <> side_b_participant_id))",
      "CREATE INDEX idx_events_date ON events(event_date DESC, title, id)",
      "CREATE INDEX idx_events_category_date ON "
      "events(event_category, event_date DESC, title, id)",
      QString("CREATE INDEX idx_event_bouts_side_a ON ") +
          "event_bouts(event_id, side_a_participant_id)",
      QString("CREATE INDEX idx_event_bouts_side_b ON ") +
          "event_bouts(event_id, side_b_participant_id)"};
  QSqlQuery query(db_);
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      setError(query.lastError().text());
      return false;
    }
  }
  return true;
}

bool EventSqliteStorage::migrateSchemaV1ToV2()
{
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 1)
  {
    setError("Event DB foreign keys are disabled before migration");
    return false;
  }
  query.finish();
  const auto restoreForeignKeys = [this](const QString& migrationError)
  {
    QSqlQuery restoreQuery(db_);
    if (!restoreQuery.exec("PRAGMA foreign_keys = ON") ||
        !restoreQuery.exec("PRAGMA foreign_keys") ||
        !restoreQuery.next() || restoreQuery.value(0).toInt() != 1)
    {
      setError(migrationError +
               "; cannot restore event DB foreign key enforcement");
      return;
    }
    setError(migrationError);
  };
  if (!query.exec("PRAGMA foreign_keys = OFF"))
  {
    setError("Cannot disable event DB foreign keys for migration");
    return false;
  }
  if (!query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 0)
  {
    query.finish();
    restoreForeignKeys(
        "Cannot confirm disabled event DB foreign keys for migration");
    return false;
  }
  query.finish();

  if (!db_.transaction())
  {
    restoreForeignKeys(db_.lastError().text());
    return false;
  }

  const QStringList statements = {
      CreateEventParticipantsTableSql(
          "event_participants_v2",
          kMaxEventParticipantSnapshotNameLength),
      "INSERT INTO event_participants_v2("
      "event_id, participant_id, participant_name_snapshot, "
      "participant_full_name_snapshot, sort_order) "
      "SELECT event_id, participant_id, participant_name_snapshot, "
      "participant_full_name_snapshot, sort_order FROM event_participants",
      "DROP TABLE event_participants",
      "ALTER TABLE event_participants_v2 RENAME TO event_participants",
      QString("PRAGMA user_version = %1").arg(kEventSchemaVersionV2)};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      const QString error = query.lastError().text();
      db_.rollback();
      restoreForeignKeys(error);
      return false;
    }
  }
  if (!query.exec("PRAGMA foreign_key_check"))
  {
    const QString error = query.lastError().text();
    db_.rollback();
    restoreForeignKeys(error);
    return false;
  }
  if (query.next())
  {
    db_.rollback();
    restoreForeignKeys("Event DB foreign key violation after migration");
    return false;
  }
  query.finish();
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    restoreForeignKeys(error);
    return false;
  }
  if (!enableForeignKeys())
  {
    return false;
  }
  return true;
}

bool EventSqliteStorage::migrateSchemaV2ToV3()
{
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  auto fail = [this](const QString& error)
  {
    db_.rollback();
    setError(error);
    return false;
  };

  QSqlQuery query(db_);
  if (!query.exec("CREATE INDEX idx_event_participants_history ON "
                  "event_participants(participant_id, event_id)"))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec(
          QString("PRAGMA user_version = %1").arg(kEventSchemaVersionV3)))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec("PRAGMA foreign_key_check"))
  {
    return fail(query.lastError().text());
  }
  if (query.next())
  {
    return fail("Event DB foreign key violation after migration");
  }
  query.finish();
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return true;
}

bool EventSqliteStorage::migrateSchemaV3ToV4()
{
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  auto fail = [this](const QString& error)
  {
    db_.rollback();
    setError(error);
    return false;
  };

  QSqlQuery query(db_);
  if (!query.exec(
          "ALTER TABLE events ADD COLUMN event_category INTEGER NOT NULL "
          "DEFAULT 0 CHECK(typeof(event_category) = 'integer' AND "
          "event_category BETWEEN 0 AND 2)"))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec("CREATE INDEX idx_events_category_date ON "
                  "events(event_category, event_date DESC, title, id)"))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec(
          QString("PRAGMA user_version = %1").arg(kEventSchemaVersionV4)))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec("PRAGMA foreign_key_check"))
  {
    return fail(query.lastError().text());
  }
  if (query.next())
  {
    return fail("Event DB foreign key violation after migration");
  }
  query.finish();
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return true;
}

bool EventSqliteStorage::migrateSchemaV4ToV5()
{
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 1)
  {
    setError("Event DB foreign keys are disabled before migration");
    return false;
  }
  query.finish();
  const auto restoreForeignKeys = [this](const QString& migrationError)
  {
    QSqlQuery restoreQuery(db_);
    if (!restoreQuery.exec("PRAGMA foreign_keys = ON") ||
        !restoreQuery.exec("PRAGMA foreign_keys") ||
        !restoreQuery.next() || restoreQuery.value(0).toInt() != 1)
    {
      setError(migrationError +
               "; cannot restore event DB foreign key enforcement");
      return;
    }
    setError(migrationError);
  };
  if (!query.exec("PRAGMA foreign_keys = OFF") ||
      !query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 0)
  {
    query.finish();
    restoreForeignKeys(
        "Cannot disable event DB foreign keys for migration");
    return false;
  }
  query.finish();
  if (!db_.transaction())
  {
    restoreForeignKeys(db_.lastError().text());
    return false;
  }

  const QStringList statements = {
      CreateEventsTableSql("events_v5", 3),
      "INSERT INTO events_v5(id, title, event_date, event_category, revision, "
      "notes, created_at, updated_at) SELECT id, title, event_date, "
      "event_category, revision, notes, created_at, updated_at FROM events",
      "DROP TABLE events",
      "ALTER TABLE events_v5 RENAME TO events",
      "CREATE INDEX idx_events_date ON events(event_date DESC, title, id)",
      "CREATE INDEX idx_events_category_date ON "
      "events(event_category, event_date DESC, title, id)",
      CreateEventParticipantsTableSql(
          "event_attendees", kMaxEventParticipantSnapshotNameLength),
      "CREATE INDEX idx_event_attendees_history ON "
      "event_attendees(participant_id, event_id)",
      QString("PRAGMA user_version = %1").arg(kEventSchemaVersion)};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      const QString error = query.lastError().text();
      db_.rollback();
      restoreForeignKeys(error);
      return false;
    }
  }
  if (!query.exec("PRAGMA foreign_key_check"))
  {
    const QString error = query.lastError().text();
    db_.rollback();
    restoreForeignKeys(error);
    return false;
  }
  if (query.next())
  {
    db_.rollback();
    restoreForeignKeys("Event DB foreign key violation after migration");
    return false;
  }
  query.finish();
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    restoreForeignKeys(error);
    return false;
  }
  return enableForeignKeys();
}

bool EventSqliteStorage::verifySchema(
    int participantSnapshotNameMaxLength,
    bool requireParticipantHistoryIndex,
    bool requireEventCategory, int maxEventCategory,
    bool requireNonCompetingAttendees)
{
  QSet<QString> eventColumns = {"id", "title", "event_date", "revision",
                                "notes", "created_at", "updated_at"};
  if (requireEventCategory)
  {
    eventColumns.insert("event_category");
  }
  QList<QPair<QString, QSet<QString>>> schema = {
      {"events", eventColumns},
      {"event_participants",
       {"event_id", "participant_id", "participant_name_snapshot",
        "participant_full_name_snapshot", "sort_order"}},
      {"event_bouts",
       {"id", "event_id", "sort_order", "side_a_participant_id",
         "side_a_free_name", "side_b_participant_id", "side_b_free_name",
         "score_a", "score_b"}}};
  if (requireNonCompetingAttendees)
  {
    schema.push_back(
        {"event_attendees",
         {"event_id", "participant_id", "participant_name_snapshot",
          "participant_full_name_snapshot", "sort_order"}});
  }
  for (const auto& entry : schema)
  {
    if (!tableExists(entry.first) ||
        !TableHasColumns(db_, entry.first, entry.second))
    {
      setError(QString("Event schema is incomplete: %1").arg(entry.first));
      return false;
    }
  }
  QSqlQuery query(db_);
  if (requireEventCategory)
  {
    if (!query.exec("PRAGMA table_info(events)"))
    {
      setError("Cannot read event category column definition");
      return false;
    }
    bool categoryColumnMatches = false;
    while (query.next())
    {
      if (query.value(1).toString() != "event_category")
      {
        continue;
      }
      categoryColumnMatches =
          query.value(2).toString().compare("INTEGER",
                                            Qt::CaseInsensitive) == 0 &&
          query.value(3).toInt() == 1 &&
          query.value(4).toString() == "0";
      break;
    }
    query.finish();
    if (!categoryColumnMatches)
    {
      setError("Event category column contract is incomplete");
      return false;
    }
  }
  if (!query.exec("SELECT name, sql FROM sqlite_master WHERE type = 'table' "
                  "AND name IN ('events', 'event_participants', "
                  "'event_attendees', 'event_bouts')"))
  {
    setError("Cannot read event schema definitions");
    return false;
  }
  QHash<QString, QString> definitions;
  while (query.next())
  {
    definitions.insert(query.value(0).toString(),
                       query.value(1).toString().simplified());
  }
  const QString eventsSql = definitions.value("events");
  const QString participantsSql = definitions.value("event_participants");
  const QString attendeesSql = definitions.value("event_attendees");
  const QString boutsSql = definitions.value("event_bouts");
  const QString participantNameConstraint =
      QString("length(trim(participant_name_snapshot)) BETWEEN 1 AND %1 AND")
          .arg(participantSnapshotNameMaxLength);
  const QString categoryConstraint =
      QString("CHECK(typeof(event_category) = 'integer' AND "
              "event_category BETWEEN 0 AND %1)")
          .arg(maxEventCategory);
  const auto rosterConstraintsMatch =
      [&participantNameConstraint](const QString& sql)
  {
    return sql.contains("PRIMARY KEY(event_id, participant_id)",
                        Qt::CaseInsensitive) &&
           sql.contains("UNIQUE(event_id, sort_order)",
                        Qt::CaseInsensitive) &&
           sql.contains(participantNameConstraint, Qt::CaseInsensitive) &&
           sql.contains("ON DELETE CASCADE", Qt::CaseInsensitive);
  };
  if (definitions.size() != (requireNonCompetingAttendees ? 4 : 3) ||
      !eventsSql.contains("length(trim(title)) BETWEEN 1 AND 200",
                          Qt::CaseInsensitive) ||
      !eventsSql.contains("revision >= 1", Qt::CaseInsensitive) ||
      (requireEventCategory &&
       (!eventsSql.contains("event_category INTEGER NOT NULL DEFAULT 0",
                             Qt::CaseInsensitive) ||
         !eventsSql.contains(categoryConstraint, Qt::CaseInsensitive))) ||
      !rosterConstraintsMatch(participantsSql) ||
      (requireNonCompetingAttendees &&
       !rosterConstraintsMatch(attendeesSql)) ||
      !boutsSql.contains("UNIQUE(event_id, sort_order)",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("REFERENCES event_participants(event_id, "
                         "participant_id)",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("score_a BETWEEN 0 AND 2147483647",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("score_b BETWEEN 0 AND 2147483647",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("side_a_participant_id IS NOT NULL AND "
                         "side_a_free_name IS NULL",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("side_b_participant_id IS NOT NULL AND "
                         "side_b_free_name IS NULL",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("side_a_participant_id IS NULL OR "
                         "side_b_participant_id IS NULL OR "
                         "side_a_participant_id <> side_b_participant_id",
                         Qt::CaseInsensitive) ||
      !boutsSql.contains("ON DELETE CASCADE", Qt::CaseInsensitive))
  {
    setError("Event schema constraints are incomplete");
    return false;
  }
  if (!query.exec("PRAGMA foreign_key_list(event_participants)"))
  {
    setError("Cannot read event participant foreign keys");
    return false;
  }
  int participantForeignKeys = 0;
  while (query.next())
  {
    if (query.value(2).toString() == "events" &&
        query.value(3).toString() == "event_id" &&
        query.value(4).toString() == "id" &&
        query.value(6).toString().compare("CASCADE", Qt::CaseInsensitive) == 0)
    {
      ++participantForeignKeys;
    }
  }
  if (participantForeignKeys != 1)
  {
    setError("Event participant foreign key is incomplete");
    return false;
  }
  if (requireNonCompetingAttendees)
  {
    if (!query.exec("PRAGMA foreign_key_list(event_attendees)"))
    {
      setError("Cannot read event attendee foreign keys");
      return false;
    }
    int attendeeForeignKeys = 0;
    while (query.next())
    {
      if (query.value(2).toString() == "events" &&
          query.value(3).toString() == "event_id" &&
          query.value(4).toString() == "id" &&
          query.value(6).toString().compare(
              "CASCADE", Qt::CaseInsensitive) == 0)
      {
        ++attendeeForeignKeys;
      }
    }
    if (attendeeForeignKeys != 1)
    {
      setError("Event attendee foreign key is incomplete");
      return false;
    }
  }
  if (!query.exec("PRAGMA foreign_key_list(event_bouts)"))
  {
    setError("Cannot read event bout foreign keys");
    return false;
  }
  int eventForeignKeyRows = 0;
  QHash<int, QSet<QString>> rosterForeignKeys;
  while (query.next())
  {
    const int foreignKeyId = query.value(0).toInt();
    const QString parentTable = query.value(2).toString();
    const QString fromColumn = query.value(3).toString();
    const QString toColumn = query.value(4).toString();
    if (parentTable == "events" &&
        fromColumn == "event_id" && toColumn == "id" &&
        query.value(6).toString().compare("CASCADE", Qt::CaseInsensitive) == 0)
    {
      ++eventForeignKeyRows;
    }
    else if (parentTable == "event_participants")
    {
      rosterForeignKeys[foreignKeyId].insert(
          QString("%1->%2").arg(fromColumn, toColumn));
    }
  }
  const QSet<QString> sideAKey = {"event_id->event_id",
                                  "side_a_participant_id->participant_id"};
  const QSet<QString> sideBKey = {"event_id->event_id",
                                  "side_b_participant_id->participant_id"};
  bool hasSideAKey = false;
  bool hasSideBKey = false;
  for (const QSet<QString>& columns : rosterForeignKeys)
  {
    hasSideAKey = hasSideAKey || columns == sideAKey;
    hasSideBKey = hasSideBKey || columns == sideBKey;
  }
  if (eventForeignKeyRows != 1 || rosterForeignKeys.size() != 2 ||
      !hasSideAKey || !hasSideBKey)
  {
    setError("Event bout foreign keys are incomplete");
    return false;
  }
  QStringList expectedIndexes = {"idx_events_date", "idx_event_bouts_side_a",
                                 "idx_event_bouts_side_b"};
  if (requireParticipantHistoryIndex)
  {
    expectedIndexes.push_back("idx_event_participants_history");
  }
  if (requireEventCategory)
  {
    expectedIndexes.push_back("idx_events_category_date");
  }
  if (requireNonCompetingAttendees)
  {
    expectedIndexes.push_back("idx_event_attendees_history");
  }
  QStringList quotedIndexNames;
  quotedIndexNames.reserve(expectedIndexes.size());
  for (const QString& index : expectedIndexes)
  {
    quotedIndexNames.push_back(QString("'%1'").arg(index));
  }
  const QString indexSql =
      QString("SELECT name FROM sqlite_master WHERE type = 'index' AND "
              "name IN (%1)")
          .arg(quotedIndexNames.join(','));
  if (!query.exec(indexSql))
  {
    setError("Cannot read event schema indexes");
    return false;
  }
  QSet<QString> indexes;
  while (query.next())
  {
    indexes.insert(query.value(0).toString());
  }
  if (indexes.size() != expectedIndexes.size())
  {
    setError("Event schema indexes are incomplete");
    return false;
  }
  const auto hasIndexColumns = [this](const QString& name,
                                      const QStringList& expected)
  {
    QSqlQuery indexQuery(db_);
    if (!indexQuery.exec(QString("PRAGMA index_info(%1)").arg(name)))
    {
      return false;
    }
    QStringList actual;
    while (indexQuery.next())
    {
      actual.push_back(indexQuery.value(2).toString());
    }
    return actual == expected;
  };
  if (!hasIndexColumns("idx_events_date", {"event_date", "title", "id"}) ||
      (requireParticipantHistoryIndex &&
       !hasIndexColumns("idx_event_participants_history",
                        {"participant_id", "event_id"})) ||
      (requireEventCategory &&
       !hasIndexColumns("idx_events_category_date",
                         {"event_category", "event_date", "title", "id"})) ||
      (requireNonCompetingAttendees &&
       !hasIndexColumns("idx_event_attendees_history",
                        {"participant_id", "event_id"})) ||
      !hasIndexColumns("idx_event_bouts_side_a",
                       {"event_id", "side_a_participant_id"}) ||
      !hasIndexColumns("idx_event_bouts_side_b",
                       {"event_id", "side_b_participant_id"}))
  {
    setError("Event schema index definitions are incomplete");
    return false;
  }
  if (!query.exec("PRAGMA foreign_key_check") || query.next())
  {
    setError("Event schema foreign key check failed");
    return false;
  }
  return true;
}

std::optional<std::vector<EventRecord>> EventSqliteStorage::listEvents()
{
  lastError_.clear();
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return std::nullopt;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT id FROM events ORDER BY event_date DESC, "
                  "lower(title), id"))
  {
    const QString error = query.lastError().text();
    db_.rollback();
    setError(error);
    return std::nullopt;
  }
  std::vector<EventId> ids;
  while (query.next())
  {
    ids.push_back({query.value(0).toString()});
  }
  query.finish();
  std::vector<EventRecord> result;
  result.reserve(ids.size());
  for (const EventId& id : ids)
  {
    const auto event = loadEvent(id);
    if (!event.has_value())
    {
      db_.rollback();
      return std::nullopt;
    }
    result.push_back(*event);
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return std::nullopt;
  }
  return result;
}

std::optional<EventRecord> EventSqliteStorage::getEvent(const EventId& id)
{
  lastError_.clear();
  if (!id.isValid())
  {
    setError("Invalid event ID");
    return std::nullopt;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return std::nullopt;
  }
  const auto result = loadEvent(id);
  if (!result.has_value())
  {
    db_.rollback();
    return std::nullopt;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return std::nullopt;
  }
  return result;
}

std::optional<ParticipantEventStatistics>
EventSqliteStorage::participantStatistics(const ParticipantId& id)
{
  lastError_.clear();
  if (!id.isValid())
  {
    setError("Invalid participant ID");
    return std::nullopt;
  }

  QSqlQuery query(db_);
  query.prepare(
      "SELECT COUNT(DISTINCT ep.event_id), "
      "COUNT(DISTINCT CASE WHEN e.event_category = 1 THEN ep.event_id END), "
      "COUNT(DISTINCT CASE WHEN e.event_category = 2 THEN ep.event_id END), "
      "COUNT(DISTINCT CASE WHEN e.event_category = 3 THEN ep.event_id END), "
      "COUNT(DISTINCT CASE WHEN e.event_category = 0 THEN ep.event_id END), "
      "COUNT(DISTINCT b.id), "
      "(SELECT COUNT(DISTINCT ea.event_id) FROM event_attendees ea "
      "WHERE ea.participant_id = :attendee_id), "
      "MIN(e.event_date), MAX(e.event_date) "
      "FROM event_participants ep "
      "JOIN events e ON e.id = ep.event_id "
      "LEFT JOIN event_bouts b ON b.event_id = ep.event_id AND "
      "(b.side_a_participant_id = :side_a_id OR "
      "b.side_b_participant_id = :side_b_id) "
      "WHERE ep.participant_id = :participant_id");
  query.bindValue(":side_a_id", id.value);
  query.bindValue(":side_b_id", id.value);
  query.bindValue(":participant_id", id.value);
  query.bindValue(":attendee_id", id.value);
  if (!query.exec() || !query.next())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }

  const qint64 tournamentCount = query.value(0).toLongLong();
  const qint64 clubTournamentCount = query.value(1).toLongLong();
  const qint64 externalCompetitionCount = query.value(2).toLongLong();
  const qint64 softCombatTournamentCount = query.value(3).toLongLong();
  const qint64 unspecifiedTournamentCount = query.value(4).toLongLong();
  const qint64 boutCount = query.value(5).toLongLong();
  const qint64 nonCompetingTripCount = query.value(6).toLongLong();
  const auto countIsOutOfRange = [](qint64 count)
  {
    return count < 0 || count > std::numeric_limits<int>::max();
  };
  if (countIsOutOfRange(tournamentCount) ||
      countIsOutOfRange(clubTournamentCount) ||
      countIsOutOfRange(externalCompetitionCount) ||
      countIsOutOfRange(softCombatTournamentCount) ||
      countIsOutOfRange(unspecifiedTournamentCount) ||
      countIsOutOfRange(boutCount) ||
      countIsOutOfRange(nonCompetingTripCount))
  {
    setError("Participant event statistics exceed supported range");
    return std::nullopt;
  }
  if (clubTournamentCount + externalCompetitionCount +
          softCombatTournamentCount +
          unspecifiedTournamentCount !=
      tournamentCount)
  {
    setError("Stored participant event categories are inconsistent");
    return std::nullopt;
  }

  ParticipantEventStatistics result;
  result.participantId = id;
  result.tournamentCount = static_cast<int>(tournamentCount);
  result.clubTournamentCount = static_cast<int>(clubTournamentCount);
  result.externalCompetitionCount =
      static_cast<int>(externalCompetitionCount);
  result.softCombatTournamentCount =
      static_cast<int>(softCombatTournamentCount);
  result.unspecifiedTournamentCount =
      static_cast<int>(unspecifiedTournamentCount);
  result.boutCount = static_cast<int>(boutCount);
  result.nonCompetingTripCount = static_cast<int>(nonCompetingTripCount);
  if (result.tournamentCount == 0)
  {
    if (result.boutCount != 0)
    {
      setError("Stored participant event statistics are inconsistent");
      return std::nullopt;
    }
    return result;
  }

  const QDate first =
      QDate::fromString(query.value(7).toString(), Qt::ISODate);
  const QDate last = QDate::fromString(query.value(8).toString(), Qt::ISODate);
  if (!first.isValid() || !last.isValid() || first > last)
  {
    setError("Stored participant event dates are invalid");
    return std::nullopt;
  }
  result.firstTournament = first;
  result.lastTournament = last;
  return result;
}

std::optional<EventRecord> EventSqliteStorage::loadEvent(const EventId& id)
{
  EventRecord result;
  QSqlQuery query(db_);
  query.prepare("SELECT id, title, event_date, event_category, revision, "
                "notes FROM events WHERE id = :id");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  if (!query.next())
  {
    setError("Event not found");
    return std::nullopt;
  }
  result.id = {query.value(0).toString()};
  result.title = query.value(1).toString();
  result.date = QDate::fromString(query.value(2).toString(), Qt::ISODate);
  result.category = static_cast<EventCategory>(query.value(3).toInt());
  result.revision = query.value(4).toLongLong();
  result.notes = query.value(5).toString();

  query.prepare(
      "SELECT participant_id, participant_name_snapshot, "
      "participant_full_name_snapshot FROM event_participants WHERE "
      "event_id = :id ORDER BY sort_order");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  while (query.next())
  {
    EventParticipantSnapshot participant;
    participant.participantId = {query.value(0).toString()};
    participant.displayNameSnapshot = query.value(1).toString();
    participant.fullNameSnapshot = query.value(2).toString();
    result.participants.push_back(std::move(participant));
  }

  query.prepare(
      "SELECT participant_id, participant_name_snapshot, "
      "participant_full_name_snapshot FROM event_attendees WHERE "
      "event_id = :id ORDER BY sort_order");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  while (query.next())
  {
    EventParticipantSnapshot attendee;
    attendee.participantId = {query.value(0).toString()};
    attendee.displayNameSnapshot = query.value(1).toString();
    attendee.fullNameSnapshot = query.value(2).toString();
    result.nonCompetingAttendees.push_back(std::move(attendee));
  }

  query.prepare(
      "SELECT id, side_a_participant_id, side_a_free_name, "
      "side_b_participant_id, side_b_free_name, score_a, score_b FROM "
      "event_bouts WHERE event_id = :id ORDER BY sort_order");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  while (query.next())
  {
    EventBout bout;
    bout.id = {query.value(0).toString()};
    if (!query.value(1).isNull())
    {
      bout.sideA.participantId =
          ParticipantId{query.value(1).toString()};
    }
    else
    {
      bout.sideA.freeName = query.value(2).toString();
    }
    if (!query.value(3).isNull())
    {
      bout.sideB.participantId =
          ParticipantId{query.value(3).toString()};
    }
    else
    {
      bout.sideB.freeName = query.value(4).toString();
    }
    bout.scoreA = query.value(5).toInt();
    bout.scoreB = query.value(6).toInt();
    result.bouts.push_back(std::move(bout));
  }
  if (!result.isValid())
  {
    setError("Stored event aggregate is invalid");
    return std::nullopt;
  }
  return result;
}

bool EventSqliteStorage::saveEvent(const EventRecord& event)
{
  lastError_.clear();
  const EventRecord normalized = NormalizeEvent(event);
  if (!normalized.isValid())
  {
    setError("Invalid event aggregate");
    return false;
  }
  if (!IsClassifiedEventCategory(normalized.category))
  {
    setError("Event category must be selected before saving");
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  auto fail = [this](const QString& error)
  {
    db_.rollback();
    setError(error);
    return false;
  };

  QSqlQuery query(db_);
  if (normalized.revision == 0)
  {
    query.prepare(
        "INSERT INTO events(id, title, event_date, event_category, revision, "
        "notes) VALUES(:id, :title, :date, :category, 1, :notes)");
  }
  else
  {
    query.prepare(
        "UPDATE events SET title = :title, event_date = :date, "
        "event_category = :category, notes = :notes, "
        "revision = revision + 1, "
        "updated_at = CURRENT_TIMESTAMP WHERE id = :id AND "
        "revision = :revision");
    query.bindValue(":revision", normalized.revision);
  }
  query.bindValue(":id", normalized.id.value);
  query.bindValue(":title", normalized.title);
  query.bindValue(":date", normalized.date.toString(Qt::ISODate));
  query.bindValue(":category", static_cast<int>(normalized.category));
  query.bindValue(":notes", normalized.notes);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  if (query.numRowsAffected() != 1)
  {
    return fail("Event was changed or deleted by another process");
  }

  query.prepare("DELETE FROM event_bouts WHERE event_id = :id");
  query.bindValue(":id", normalized.id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("DELETE FROM event_participants WHERE event_id = :id");
  query.bindValue(":id", normalized.id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("DELETE FROM event_attendees WHERE event_id = :id");
  query.bindValue(":id", normalized.id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }

  query.prepare(
      "INSERT INTO event_participants(event_id, participant_id, "
      "participant_name_snapshot, participant_full_name_snapshot, "
      "sort_order) VALUES(:event_id, :participant_id, :name, :full_name, "
      ":sort_order)");
  for (int index = 0;
       index < static_cast<int>(normalized.participants.size()); ++index)
  {
    const EventParticipantSnapshot& participant =
        normalized.participants.at(index);
    query.bindValue(":event_id", normalized.id.value);
    query.bindValue(":participant_id", participant.participantId.value);
    query.bindValue(":name", participant.displayNameSnapshot);
    query.bindValue(":full_name", participant.fullNameSnapshot);
    query.bindValue(":sort_order", index);
    if (!query.exec())
    {
      return fail(query.lastError().text());
    }
  }

  query.prepare(
      "INSERT INTO event_attendees(event_id, participant_id, "
      "participant_name_snapshot, participant_full_name_snapshot, "
      "sort_order) VALUES(:event_id, :participant_id, :name, :full_name, "
      ":sort_order)");
  for (int index = 0;
       index < static_cast<int>(normalized.nonCompetingAttendees.size());
       ++index)
  {
    const EventParticipantSnapshot& attendee =
        normalized.nonCompetingAttendees.at(index);
    query.bindValue(":event_id", normalized.id.value);
    query.bindValue(":participant_id", attendee.participantId.value);
    query.bindValue(":name", attendee.displayNameSnapshot);
    query.bindValue(":full_name", attendee.fullNameSnapshot);
    query.bindValue(":sort_order", index);
    if (!query.exec())
    {
      return fail(query.lastError().text());
    }
  }

  query.prepare(
      "INSERT INTO event_bouts(id, event_id, sort_order, "
      "side_a_participant_id, side_a_free_name, side_b_participant_id, "
      "side_b_free_name, score_a, score_b) VALUES(:id, :event_id, "
      ":sort_order, :side_a_id, :side_a_name, :side_b_id, :side_b_name, "
      ":score_a, :score_b)");
  for (int index = 0; index < static_cast<int>(normalized.bouts.size());
       ++index)
  {
    const EventBout& bout = normalized.bouts.at(index);
    query.bindValue(":id", bout.id.value);
    query.bindValue(":event_id", normalized.id.value);
    query.bindValue(":sort_order", index);
    query.bindValue(
        ":side_a_id",
        bout.sideA.participantId.has_value()
            ? QVariant(bout.sideA.participantId->value)
            : QVariant());
    query.bindValue(":side_a_name", bout.sideA.participantId.has_value()
                                          ? QVariant()
                                          : QVariant(bout.sideA.freeName));
    query.bindValue(
        ":side_b_id",
        bout.sideB.participantId.has_value()
            ? QVariant(bout.sideB.participantId->value)
            : QVariant());
    query.bindValue(":side_b_name", bout.sideB.participantId.has_value()
                                          ? QVariant()
                                          : QVariant(bout.sideB.freeName));
    query.bindValue(":score_a", bout.scoreA);
    query.bindValue(":score_b", bout.scoreB);
    if (!query.exec())
    {
      return fail(query.lastError().text());
    }
  }
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return true;
}

bool EventSqliteStorage::removeEvent(const EventId& id,
                                    qint64 expectedRevision)
{
  lastError_.clear();
  if (!id.isValid() || expectedRevision < 1)
  {
    setError("Invalid event identity or revision");
    return false;
  }
  QSqlQuery query(db_);
  query.prepare("DELETE FROM events WHERE id = :id AND revision = :revision");
  query.bindValue(":id", id.value);
  query.bindValue(":revision", expectedRevision);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  if (query.numRowsAffected() != 1)
  {
    setError("Event was changed or deleted by another process");
    return false;
  }
  return true;
}
