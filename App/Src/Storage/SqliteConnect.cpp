#include "SqliteConnect.hpp"

#include <QDate>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace
{

constexpr int kProfileSchemaVersion = 3;
constexpr int kDayMarkerSchemaVersion = 4;
constexpr int kRankSchemaVersion = 5;
constexpr int kDevelopmentSchemaVersion = 6;
constexpr int kParticipantDetailsSchemaVersion = 7;
constexpr int kParticipantNameSchemaVersion = 8;
constexpr int kCombatHandSchemaVersion = 9;
constexpr int kSchemaVersion = 10;
constexpr int kSqliteBusyTimeoutMs = 5000;

bool tableHasColumns(QSqlDatabase& db, const QString& table,
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
} // namespace

SqliteConnect::SqliteConnect() = default;

SqliteConnect::~SqliteConnect()
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

bool SqliteConnect::open(const QString& dbPath)
{
  lastError_.clear();

  if (!db_.isOpen())
  {
    connectionName_ =
        QString("journal_connection_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db_.setDatabaseName(dbPath);
    if (!db_.open())
    {
      setError(db_.lastError().text());
      return false;
    }
  }

  if (!enableForeignKeys() || !ensureSchema())
  {
    return false;
  }
  if (tableExists("legacy_users_v0") || tableExists("legacy_month_days_v0") ||
      tableExists("months") || tableExists("legacy_months"))
  {
    db_.close();
    if (!db_.open() || !enableForeignKeys())
    {
      setError(db_.lastError().text());
      return false;
    }
    return cleanupLegacyTables();
  }
  return true;
}
QString SqliteConnect::lastError() const
{
  return lastError_;
}

std::optional<std::vector<JournalMonth>> SqliteConnect::listMonths()
{
  lastError_.clear();
  QSqlQuery query(db_);
  // Месяц считается сформированным по той же семантике, что getMonthState().
  // UNION не даёт UI дубликаты, даже если aggregate заполнен во всех таблицах.
  if (!query.exec("SELECT year, month FROM ("
                  "SELECT year, month FROM month_days UNION "
                  "SELECT year, month FROM month_participants UNION "
                  "SELECT year, month FROM attendance) "
                  "ORDER BY year DESC, month DESC"))
  {
    setError(query.lastError().text());
    return std::nullopt;
  }

  std::vector<JournalMonth> result;
  while (query.next())
  {
    const JournalMonth value{query.value(0).toInt(), query.value(1).toInt()};
    if (!validateYearMonth(value.year, value.month))
    {
      setError("Invalid configured month in database");
      return std::nullopt;
    }
    result.push_back(value);
  }
  return result;
}

MonthSnapshot SqliteConnect::loadMonthSnapshot(int year, int month)
{
  MonthSnapshot snapshot;
  lastError_.clear();
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    snapshot.errorMessage = lastError_;
    return snapshot;
  }

  // Одна read transaction не даёт получить состав участников до изменения,
  // а посещения после него, если позже появится параллельный writer.
  auto fail = [this, &snapshot](const QString& error)
  {
    db_.rollback();
    setError(error);
    snapshot = {};
    snapshot.state = MonthState::Error;
    snapshot.errorMessage = lastError_;
    return snapshot;
  };

  const MonthStateResult state = getMonthState(year, month);
  if (state.state == MonthState::Error)
  {
    return fail(state.errorMessage);
  }
  snapshot.state = state.state;
  snapshot.participants = getParticipantsForMonth(year, month);
  if (!lastError_.isEmpty())
  {
    return fail(lastError_);
  }
  snapshot.activeDays = getActiveDays(year, month);
  if (!lastError_.isEmpty())
  {
    return fail(lastError_);
  }
  snapshot.attendance = getMonth(year, month);
  if (!lastError_.isEmpty())
  {
    return fail(lastError_);
  }
  snapshot.dayMarkers = getDayMarkers(year, month);
  if (!lastError_.isEmpty())
  {
    return fail(lastError_);
  }
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return snapshot;
}

void SqliteConnect::setError(const QString& error)
{
  lastError_ = error;
  qWarning() << "SqliteConnect:" << error;
}

MonthStateResult SqliteConnect::getMonthState(int year, int month)
{
  lastError_.clear();
  if (!validateYearMonth(year, month))
  {
    setError("Invalid year or month");
    return {MonthState::Error, lastError_};
  }

  QSqlQuery query(db_);
  query.prepare(
      "SELECT EXISTS(SELECT 1 FROM month_days WHERE year = :year AND "
      "month = :month) OR EXISTS(SELECT 1 FROM month_participants WHERE "
      "year = :year AND month = :month) OR EXISTS(SELECT 1 FROM attendance "
      "WHERE year = :year AND month = :month)");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec() || !query.next())
  {
    setError(query.lastError().text());
    return {MonthState::Error, lastError_};
  }
  return {query.value(0).toBool() ? MonthState::Ready : MonthState::Missing,
          QString()};
}

bool SqliteConnect::enableForeignKeys()
{
  QSqlQuery query(db_);
  if (!query.exec(
          QString("PRAGMA busy_timeout = %1").arg(kSqliteBusyTimeoutMs)))
  {
    setError(query.lastError().text());
    return false;
  }
  if (!query.exec("PRAGMA foreign_keys = ON") ||
      !query.exec("PRAGMA foreign_keys") || !query.next() ||
      query.value(0).toInt() != 1)
  {
    setError("SQLite foreign key enforcement is unavailable");
    return false;
  }
  return true;
}

bool SqliteConnect::tableExists(const QString& tableName) const
{
  QSqlQuery query(db_);
  query.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' "
                "AND name = :name");
  query.bindValue(":name", tableName);
  return query.exec() && query.next();
}

bool SqliteConnect::ensureSchema()
{
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA user_version") || !query.next())
  {
    setError("Cannot read SQLite schema version");
    return false;
  }
  const int version = query.value(0).toInt();
  query.finish();
  if (version > kSchemaVersion)
  {
    setError(QString("Unsupported SQLite schema version: %1").arg(version));
    return false;
  }
  if (version == kSchemaVersion)
  {
    return verifySchemaV10();
  }
  if (version == kCombatHandSchemaVersion)
  {
    return migrateSchemaV9ToV10() && verifySchemaV10();
  }
  if (version == kParticipantNameSchemaVersion)
  {
    return migrateSchemaV8ToV9() && migrateSchemaV9ToV10() &&
           verifySchemaV10();
  }
  if (version == kParticipantDetailsSchemaVersion)
  {
    return migrateSchemaV7ToV8() && migrateSchemaV8ToV9() &&
           migrateSchemaV9ToV10() && verifySchemaV10();
  }
  if (version == kDevelopmentSchemaVersion)
  {
    return migrateSchemaV6ToV7() && migrateSchemaV7ToV8() &&
           migrateSchemaV8ToV9() && migrateSchemaV9ToV10() &&
           verifySchemaV10();
  }
  if (version == kRankSchemaVersion)
  {
    return migrateSchemaV5ToV7() && migrateSchemaV7ToV8() &&
           migrateSchemaV8ToV9() && migrateSchemaV9ToV10() &&
           verifySchemaV10();
  }
  if (version == kDayMarkerSchemaVersion)
  {
    return migrateSchemaV4ToV5() && migrateSchemaV5ToV7() &&
           migrateSchemaV7ToV8() && migrateSchemaV8ToV9() &&
           migrateSchemaV9ToV10() && verifySchemaV10();
  }
  if (version == kProfileSchemaVersion)
  {
    return migrateSchemaV3ToV4() && migrateSchemaV4ToV5() &&
           migrateSchemaV5ToV7() && migrateSchemaV7ToV8() &&
           migrateSchemaV8ToV9() && migrateSchemaV9ToV10() &&
           verifySchemaV10();
  }
  if (version == 2)
  {
    return migrateSchemaV2ToV3() && migrateSchemaV3ToV4() &&
           migrateSchemaV4ToV5() && migrateSchemaV5ToV7() &&
           migrateSchemaV7ToV8() && migrateSchemaV8ToV9() &&
           migrateSchemaV9ToV10() && verifySchemaV10();
  }
  if (version != 0)
  {
    setError(QString("Unsupported SQLite schema version: %1").arg(version));
    return false;
  }

  if (tableExists("users"))
  {
    return migrateLegacyUsersToV3() && migrateSchemaV3ToV4() &&
           migrateSchemaV4ToV5() && migrateSchemaV5ToV7() &&
           migrateSchemaV7ToV8() && migrateSchemaV8ToV9() &&
           migrateSchemaV9ToV10() && verifySchemaV10();
  }

  const QStringList incompatible = {"participants", "month_participants",
                                    "attendance", "month_days"};
  for (const QString& table : incompatible)
  {
    if (tableExists(table))
    {
      setError("Unsupported development database. Delete it and restart.");
      return false;
    }
  }

  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  if (!createSchemaV10() ||
      !query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Schema v10 creation verification failed");
    }
    return false;
  }
  if (!db_.commit())
  {
    setError(db_.lastError().text());
    return false;
  }
  return verifySchemaV10();
}

bool SqliteConnect::createSchemaV3()
{
  QSqlQuery query(db_);
  const QStringList statements = {
      "CREATE TABLE participants ("
      "id TEXT PRIMARY KEY NOT NULL, "
      "display_name TEXT NOT NULL CHECK(length(display_name) BETWEEN 1 AND "
      "200), "
      "birth_day INTEGER CHECK(birth_day BETWEEN 1 AND 31), "
      "birth_month INTEGER CHECK(birth_month BETWEEN 1 AND 12), "
      "birth_year INTEGER CHECK(birth_year BETWEEN 1 AND 9999), "
      "notes TEXT NOT NULL DEFAULT '' CHECK(length(notes) <= 4096), "
      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
      "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
      "archived_at TEXT)",
      "CREATE TABLE month_participants ("
      "year INTEGER NOT NULL CHECK(year BETWEEN 1 AND 9999), "
      "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
      "participant_id TEXT NOT NULL, "
      "sort_order INTEGER NOT NULL CHECK(sort_order >= 0), "
      "PRIMARY KEY(year, month, participant_id), "
      "FOREIGN KEY(participant_id) REFERENCES participants(id))",
      "CREATE TABLE attendance ("
      "year INTEGER NOT NULL CHECK(year BETWEEN 1 AND 9999), "
      "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
      "day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
      "participant_id TEXT NOT NULL, "
      "is_checked INTEGER NOT NULL CHECK(is_checked IN (0, 1)), "
      "PRIMARY KEY(year, month, day, participant_id), "
      "FOREIGN KEY(year, month, participant_id) "
      "REFERENCES month_participants(year, month, participant_id) "
      "ON DELETE CASCADE)",
      "CREATE TABLE month_days ("
      "year INTEGER NOT NULL CHECK(year BETWEEN 1 AND 9999), "
      "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
      "day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
      "PRIMARY KEY(year, month, day))",
      "CREATE INDEX idx_month_participants_order "
      "ON month_participants(year, month, sort_order, participant_id)",
      "CREATE INDEX idx_month_participants_history "
      "ON month_participants(participant_id, year, month)",
      "CREATE INDEX idx_attendance_history "
      "ON attendance(participant_id, year, month, day)"};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      setError(query.lastError().text());
      return false;
    }
  }
  return createProfileValidationTriggers();
}

bool SqliteConnect::createDayMarkerSchema()
{
  QSqlQuery query(db_);
  const QStringList statements = {
      "CREATE TABLE participant_day_markers ("
      "year INTEGER NOT NULL CHECK(year BETWEEN 1 AND 9999), "
      "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
      "day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
      "participant_id TEXT NOT NULL, "
      "kind_mask INTEGER NOT NULL CHECK(typeof(kind_mask) = 'integer' AND "
      "kind_mask BETWEEN 1 AND 15), "
      "note TEXT NOT NULL DEFAULT '' CHECK(length(note) <= 500), "
      "PRIMARY KEY(year, month, day, participant_id), "
      "FOREIGN KEY(year, month, participant_id) "
      "REFERENCES month_participants(year, month, participant_id) "
      "ON DELETE CASCADE)",
      "CREATE INDEX idx_day_markers_history ON "
      "participant_day_markers(participant_id, year, month, day)"};
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

bool SqliteConnect::createSchemaV4()
{
  return createSchemaV3() && createDayMarkerSchema();
}

bool SqliteConnect::createRankSchema()
{
  QSqlQuery query(db_);
  if (!query.exec(
          "ALTER TABLE participants ADD COLUMN rank TEXT NOT NULL DEFAULT "
          "'guest' CHECK(rank IN ('page', 'squire', 'novice', 'recruit', "
          "'guest', 'knight'))") ||
      !query.exec("DROP TRIGGER participants_profile_insert") ||
      !query.exec("DROP TRIGGER participants_profile_update"))
  {
    setError(query.lastError().text());
    return false;
  }
  return createProfileValidationTriggers();
}

bool SqliteConnect::createSchemaV5()
{
  return createSchemaV4() && createRankSchema();
}

bool SqliteConnect::createParticipantDetailsSchema()
{
  const QStringList statements = {
      "ALTER TABLE participants ADD COLUMN full_name TEXT NOT NULL DEFAULT '' "
      "CHECK(length(full_name) <= 300 AND instr(full_name, char(10)) = 0 AND "
      "instr(full_name, char(13)) = 0)",
      "ALTER TABLE participants ADD COLUMN contact TEXT NOT NULL DEFAULT '' "
      "CHECK(length(contact) <= 500 AND instr(contact, char(10)) = 0 AND "
      "instr(contact, char(13)) = 0)",
      "DROP TRIGGER participants_profile_insert",
      "DROP TRIGGER participants_profile_update"};
  for (const QString& statement : statements)
  {
    QSqlQuery query(db_);
    if (!query.exec(statement))
    {
      setError(QString("Participant details migration failed: %1; SQL: %2")
                   .arg(query.lastError().text(), statement));
      return false;
    }
  }
  return upgradeDayMarkerKindsSchema() && createProfileValidationTriggers();
}

bool SqliteConnect::upgradeDayMarkerKindsSchema()
{
  const QStringList statements = {
      "DROP INDEX IF EXISTS idx_day_markers_history",
      "ALTER TABLE participant_day_markers RENAME TO "
      "participant_day_markers_v5",
      "CREATE TABLE participant_day_markers ("
      "year INTEGER NOT NULL CHECK(year BETWEEN 1 AND 9999), "
      "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
      "day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
      "participant_id TEXT NOT NULL, "
      "kind_mask INTEGER NOT NULL CHECK(typeof(kind_mask) = 'integer' AND "
      "kind_mask BETWEEN 1 AND 31), "
      "note TEXT NOT NULL DEFAULT '' CHECK(length(note) <= 500), "
      "PRIMARY KEY(year, month, day, participant_id), "
      "FOREIGN KEY(year, month, participant_id) "
      "REFERENCES month_participants(year, month, participant_id) "
      "ON DELETE CASCADE)",
      "INSERT INTO participant_day_markers(year, month, day, participant_id, "
      "kind_mask, note) SELECT year, month, day, participant_id, kind_mask, "
      "note FROM participant_day_markers_v5",
      "DROP TABLE participant_day_markers_v5",
      "CREATE INDEX idx_day_markers_history ON "
      "participant_day_markers(participant_id, year, month, day)"};
  for (const QString& statement : statements)
  {
    QSqlQuery query(db_);
    if (!query.exec(statement))
    {
      setError(QString("Day marker migration failed: %1; SQL: %2")
                   .arg(query.lastError().text(), statement));
      return false;
    }
  }
  return true;
}

bool SqliteConnect::createSchemaV7()
{
  return createSchemaV5() && createParticipantDetailsSchema();
}

bool SqliteConnect::createParticipantNameSchema()
{
  // SQLite проверяет CHECK существующих строк при ADD COLUMN. Пробел нужен
  // только как валидный migration default; profile trigger запрещает его как
  // доменное значение.
  const QStringList statements = {
      "DROP TRIGGER participants_profile_insert",
      "DROP TRIGGER participants_profile_update",
      "ALTER TABLE participants RENAME COLUMN display_name TO "
      "legacy_display_name",
      "ALTER TABLE participants ADD COLUMN display_name TEXT NOT NULL "
      "DEFAULT ' ' CHECK(length(display_name) BETWEEN 1 AND 300)",
      "ALTER TABLE participants ADD COLUMN historical_name TEXT NOT NULL "
      "DEFAULT '' CHECK(length(historical_name) <= 200 AND "
      "instr(historical_name, char(10)) = 0 AND "
      "instr(historical_name, char(13)) = 0)",
      "UPDATE participants SET display_name = trim(legacy_display_name), "
      "historical_name = trim(legacy_display_name)",
      "ALTER TABLE participants DROP COLUMN legacy_display_name"};
  for (const QString& statement : statements)
  {
    QSqlQuery query(db_);
    if (!query.exec(statement))
    {
      setError(QString("Participant-name migration failed: %1; SQL: %2")
                   .arg(query.lastError().text(), statement));
      return false;
    }
  }
  return createProfileValidationTriggers();
}

bool SqliteConnect::createSchemaV8()
{
  return createSchemaV7() && createParticipantNameSchema();
}

bool SqliteConnect::createCombatHandSchema()
{
  QSqlQuery query(db_);
  const QStringList statements = {
      "DROP TRIGGER participants_profile_insert",
      "DROP TRIGGER participants_profile_update",
      "ALTER TABLE participants ADD COLUMN combat_hand TEXT NOT NULL "
      "DEFAULT 'unknown' CHECK(combat_hand IN "
      "('unknown', 'right', 'left'))"};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      setError(QString("Combat-hand migration failed: %1; SQL: %2")
                   .arg(query.lastError().text(), statement));
      return false;
    }
  }
  return createProfileValidationTriggers();
}

bool SqliteConnect::createSchemaV9()
{
  return createSchemaV8() && createCombatHandSchema();
}

bool SqliteConnect::createTrainingStartSchema()
{
  QSqlQuery query(db_);
  const QStringList statements = {
      "DROP TRIGGER participants_profile_insert",
      "DROP TRIGGER participants_profile_update",
      "ALTER TABLE participants ADD COLUMN training_start_year INTEGER "
      "CHECK(training_start_year BETWEEN 1900 AND 9999)",
      "ALTER TABLE participants ADD COLUMN training_start_month INTEGER "
      "CHECK(training_start_month BETWEEN 1 AND 12)"};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      setError(QString("Training-start migration failed: %1; SQL: %2")
                   .arg(query.lastError().text(), statement));
      return false;
    }
  }
  return createProfileValidationTriggers();
}

bool SqliteConnect::createSchemaV10()
{
  return createSchemaV9() && createTrainingStartSchema();
}

bool SqliteConnect::migrateLegacyUsersToV3()
{
  if (tableExists("participants") || tableExists("month_participants") ||
      tableExists("attendance"))
  {
    setError("Legacy and normalized schemas are mixed");
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
  if (!query.exec("ALTER TABLE users RENAME TO legacy_users_v0"))
  {
    return fail(query.lastError().text());
  }
  const bool hasLegacyDays = tableExists("month_days");
  if (hasLegacyDays &&
      !query.exec("ALTER TABLE month_days RENAME TO legacy_month_days_v0"))
  {
    return fail(query.lastError().text());
  }
  if (!createSchemaV3())
  {
    db_.rollback();
    return false;
  }

  QHash<int, int> legacyYearByMonth;
  QSet<int> ambiguousLegacyMonths;
  if (hasLegacyDays)
  {
    QSqlQuery years(db_);
    if (!years.exec("SELECT month, MIN(year), COUNT(DISTINCT year) "
                    "FROM legacy_month_days_v0 GROUP BY month"))
    {
      return fail(years.lastError().text());
    }
    while (years.next())
    {
      const int month = years.value(0).toInt();
      if (years.value(2).toInt() != 1)
      {
        ambiguousLegacyMonths.insert(month);
        continue;
      }
      legacyYearByMonth.insert(month, years.value(1).toInt());
    }
  }
  if (tableExists("legacy_months"))
  {
    QSqlQuery mappings(db_);
    if (!mappings.exec("SELECT month, year FROM legacy_months"))
    {
      return fail(mappings.lastError().text());
    }
    while (mappings.next())
    {
      const int month = mappings.value(0).toInt();
      const int year = mappings.value(1).toInt();
      if (legacyYearByMonth.contains(month) &&
          legacyYearByMonth.value(month) != year)
      {
        return fail(
            QString("Conflicting legacy year mapping for month %1").arg(month));
      }
      legacyYearByMonth.insert(month, year);
      ambiguousLegacyMonths.remove(month);
    }
  }

  QHash<QString, QString> participantIds;
  QSqlQuery rows(db_);
  if (!rows.exec("SELECT name, date, is_checked FROM legacy_users_v0 "
                 "ORDER BY id"))
  {
    return fail(rows.lastError().text());
  }
  QSqlQuery participantInsert(db_);
  participantInsert.prepare(
      "INSERT INTO participants(id, display_name) VALUES(:id, :name)");
  QSqlQuery membershipInsert(db_);
  membershipInsert.prepare(
      "INSERT OR IGNORE INTO month_participants(year, month, participant_id, "
      "sort_order) VALUES(:year, :month, :id, COALESCE((SELECT "
      "MAX(sort_order) + 1 FROM month_participants WHERE year = :year AND "
      "month = :month), 0))");
  QSqlQuery attendanceInsert(db_);
  attendanceInsert.prepare(
      "INSERT INTO attendance(year, month, day, participant_id, is_checked) "
      "VALUES(:year, :month, :day, :id, :checked) ON CONFLICT(year, month, "
      "day, participant_id) DO UPDATE SET is_checked = excluded.is_checked");
  while (rows.next())
  {
    const QString name = rows.value(0).toString().trimmed();
    const QString dateText = rows.value(1).toString();
    if (name.isEmpty() || name.size() > 200)
    {
      return fail("Legacy database contains an invalid participant name");
    }

    QDate date = QDate::fromString(dateText, "dd.MM.yyyy");
    if (!date.isValid())
    {
      const int day = dateText.left(2).toInt();
      const int month = dateText.mid(3, 2).toInt();
      if (ambiguousLegacyMonths.contains(month))
      {
        return fail(
            QString("Legacy month %1 maps to multiple years").arg(month));
      }
      const int year =
          legacyYearByMonth.value(month, QDate::currentDate().year());
      date = QDate(year, month, day);
    }
    if (!date.isValid())
    {
      return fail(
          QString("Legacy database contains invalid date: %1").arg(dateText));
    }

    QString id = participantIds.value(name);
    if (id.isEmpty())
    {
      id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      participantIds.insert(name, id);
      participantInsert.bindValue(":id", id);
      participantInsert.bindValue(":name", name);
      if (!participantInsert.exec())
      {
        return fail(participantInsert.lastError().text());
      }
    }
    membershipInsert.bindValue(":year", date.year());
    membershipInsert.bindValue(":month", date.month());
    membershipInsert.bindValue(":id", id);
    if (!membershipInsert.exec())
    {
      return fail(membershipInsert.lastError().text());
    }
    attendanceInsert.bindValue(":year", date.year());
    attendanceInsert.bindValue(":month", date.month());
    attendanceInsert.bindValue(":day", date.day());
    attendanceInsert.bindValue(":id", id);
    attendanceInsert.bindValue(":checked", rows.value(2).toInt() != 0 ? 1 : 0);
    if (!attendanceInsert.exec())
    {
      return fail(attendanceInsert.lastError().text());
    }
  }
  rows.finish();
  rows.clear();

  if (hasLegacyDays &&
      !query.exec("INSERT INTO month_days(year, month, day) SELECT year, "
                  "month, day FROM legacy_month_days_v0"))
  {
    return fail(query.lastError().text());
  }
  if (tableExists("months"))
  {
    QSqlQuery months(db_);
    if (!months.exec("SELECT year, month FROM months"))
    {
      return fail(months.lastError().text());
    }
    QSqlQuery dayInsert(db_);
    dayInsert.prepare("INSERT OR IGNORE INTO month_days(year, month, day) "
                      "VALUES(:year, :month, :day)");
    while (months.next())
    {
      const int year = months.value(0).toInt();
      const int month = months.value(1).toInt();
      for (int day : fullMonthDays(year, month))
      {
        dayInsert.bindValue(":year", year);
        dayInsert.bindValue(":month", month);
        dayInsert.bindValue(":day", day);
        if (!dayInsert.exec())
        {
          return fail(dayInsert.lastError().text());
        }
      }
    }
  }

  if (!query.exec(
          QString("PRAGMA user_version = %1").arg(kProfileSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    return fail("Legacy migration integrity check failed");
  }
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return true;
}

bool SqliteConnect::cleanupLegacyTables()
{
  const QStringList tables = {"legacy_users_v0", "legacy_month_days_v0",
                              "months", "legacy_months"};
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  for (const QString& table : tables)
  {
    if (tableExists(table) && !query.exec("DROP TABLE " + table))
    {
      const QString error = query.lastError().text();
      db_.rollback();
      setError("DROP TABLE " + table + ": " + error);
      return false;
    }
  }
  if (!db_.commit())
  {
    setError(db_.lastError().text());
    return false;
  }
  return true;
}

bool SqliteConnect::createProfileValidationTriggers()
{
  QString validation =
      "length(trim(NEW.display_name)) = 0 OR "
      "length(NEW.display_name) > 300 OR length(NEW.notes) > 4096 OR NOT ("
      "(NEW.birth_day IS NULL AND NEW.birth_month IS NULL AND "
      "NEW.birth_year IS NULL) OR (NEW.birth_day IS NOT NULL AND "
      "NEW.birth_month IS NOT NULL AND NEW.birth_day <= CASE "
      "NEW.birth_month WHEN 2 THEN CASE WHEN COALESCE(NEW.birth_year, 2000) "
      "% 400 = 0 OR (COALESCE(NEW.birth_year, 2000) % 4 = 0 AND "
      "COALESCE(NEW.birth_year, 2000) % 100 != 0) THEN 29 ELSE 28 END "
      "WHEN 4 THEN 30 WHEN 6 THEN 30 WHEN 9 THEN 30 WHEN 11 THEN 30 "
      "ELSE 31 END))";
  const bool hasRank = tableHasColumns(db_, "participants", {"rank"});
  if (hasRank)
  {
    validation += " OR NEW.rank NOT IN ('page', 'squire', 'novice', 'recruit', "
                  "'guest', 'knight')";
  }
  const bool hasParticipantDetails =
      tableHasColumns(db_, "participants", {"full_name", "contact"});
  if (hasParticipantDetails)
  {
    validation += " OR length(NEW.full_name) > 300 OR "
                  "instr(NEW.full_name, char(10)) != 0 OR "
                  "instr(NEW.full_name, char(13)) != 0 OR "
                  "length(NEW.contact) > 500 OR "
                  "instr(NEW.contact, char(10)) != 0 OR "
                  "instr(NEW.contact, char(13)) != 0";
  }
  const bool hasHistoricalName =
      tableHasColumns(db_, "participants", {"historical_name"});
  if (hasHistoricalName)
  {
    validation += " OR length(NEW.historical_name) > 200 OR "
                  "instr(NEW.historical_name, char(10)) != 0 OR "
                  "instr(NEW.historical_name, char(13)) != 0 OR "
                  "(length(trim(NEW.historical_name)) = 0 AND "
                  "length(trim(NEW.full_name)) = 0) OR "
                  "trim(NEW.display_name) != CASE WHEN "
                  "length(trim(NEW.historical_name)) > 0 THEN "
                  "trim(NEW.historical_name) ELSE trim(NEW.full_name) END";
  }
  const bool hasCombatHand =
      tableHasColumns(db_, "participants", {"combat_hand"});
  if (hasCombatHand)
  {
    validation += " OR NEW.combat_hand NOT IN ('unknown', 'right', 'left')";
  }
  const bool hasTrainingStart = tableHasColumns(
      db_, "participants", {"training_start_year", "training_start_month"});
  if (hasTrainingStart)
  {
    validation +=
        " OR (NEW.training_start_year IS NULL) != "
        "(NEW.training_start_month IS NULL) OR "
        "(NEW.training_start_year IS NOT NULL AND "
        "(NEW.training_start_year NOT BETWEEN 1900 AND 9999 OR "
        "NEW.training_start_month NOT BETWEEN 1 AND 12))";
  }
  QSqlQuery query(db_);
  QStringList updateColumns = {"display_name", "birth_day", "birth_month",
                               "birth_year", "notes"};
  if (hasRank)
  {
    updateColumns.push_back("rank");
  }
  if (hasParticipantDetails)
  {
    updateColumns.append({"full_name", "contact"});
  }
  if (hasHistoricalName)
  {
    updateColumns.push_back("historical_name");
  }
  if (hasCombatHand)
  {
    updateColumns.push_back("combat_hand");
  }
  if (hasTrainingStart)
  {
    updateColumns.append({"training_start_year", "training_start_month"});
  }
  const QStringList statements = {
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(validation),
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "%1 ON participants WHEN %2 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(updateColumns.join(", "), validation)};
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

bool SqliteConnect::migrateSchemaV2ToV3()
{
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  const QStringList requiredTables = {"participants", "month_participants",
                                      "attendance", "month_days"};
  for (const QString& table : requiredTables)
  {
    if (!tableExists(table))
    {
      db_.rollback();
      setError(QString("Schema v2 misses table: %1").arg(table));
      return false;
    }
  }
  const QList<QPair<QString, QSet<QString>>> schema = {
      {"participants",
       {"id", "display_name", "created_at", "updated_at", "archived_at"}},
      {"month_participants", {"year", "month", "participant_id", "sort_order"}},
      {"attendance", {"year", "month", "day", "participant_id", "is_checked"}},
      {"month_days", {"year", "month", "day"}}};
  for (const auto& entry : schema)
  {
    if (!tableHasColumns(db_, entry.first, entry.second))
    {
      db_.rollback();
      setError(
          QString("Schema v2 columns are incomplete: %1").arg(entry.first));
      return false;
    }
  }
  if (!query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    setError("Schema v2 integrity check failed");
    return false;
  }
  if (!query.exec("SELECT 1 FROM participants WHERE "
                  "length(trim(display_name)) = 0 OR "
                  "length(display_name) > 200 LIMIT 1") ||
      query.next())
  {
    db_.rollback();
    setError("Schema v2 contains invalid participant profiles");
    return false;
  }
  const QStringList statements = {
      "ALTER TABLE participants ADD COLUMN birth_day INTEGER "
      "CHECK(birth_day BETWEEN 1 AND 31)",
      "ALTER TABLE participants ADD COLUMN birth_month INTEGER "
      "CHECK(birth_month BETWEEN 1 AND 12)",
      "ALTER TABLE participants ADD COLUMN birth_year INTEGER "
      "CHECK(birth_year BETWEEN 1 AND 9999)",
      "ALTER TABLE participants ADD COLUMN notes TEXT NOT NULL DEFAULT '' "
      "CHECK(length(notes) <= 4096)"};
  for (const QString& statement : statements)
  {
    if (!query.exec(statement))
    {
      db_.rollback();
      setError(query.lastError().text());
      return false;
    }
  }
  if (!createProfileValidationTriggers() || !verifySchemaV3() ||
      !query.exec(
          QString("PRAGMA user_version = %1").arg(kProfileSchemaVersion)))
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v2 to v3 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV3ToV4()
{
  if (!verifySchemaV3())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createDayMarkerSchema() ||
      !query.exec(
          QString("PRAGMA user_version = %1").arg(kDayMarkerSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v3 to v4 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV4ToV5()
{
  if (!verifySchemaV4())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createRankSchema() ||
      !query.exec(
          QString("PRAGMA user_version = %1").arg(kRankSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v4 to v5 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV5ToV7()
{
  if (!verifySchemaV5())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createParticipantDetailsSchema() ||
      !query.exec(QString("PRAGMA user_version = %1")
                      .arg(kParticipantDetailsSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v5 to v7 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV6ToV7()
{
  if (!verifySchemaV5() ||
      !tableHasColumns(db_, "participants", {"full_name", "contact"}))
  {
    return false;
  }

  QSqlQuery query(db_);
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participant_day_markers'") ||
      !query.next())
  {
    setError("Schema v6 day-marker definition is missing");
    return false;
  }
  const QString markerSql = query.value(0).toString().simplified();
  query.finish();
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participants'") ||
      !query.next())
  {
    setError("Schema v6 participant definition is missing");
    return false;
  }
  const QString participantSql = query.value(0).toString().simplified();
  query.finish();
  if (!query.exec("PRAGMA table_info(participants)"))
  {
    setError("Cannot read schema v6 participant columns");
    return false;
  }
  QSet<QString> participantColumns;
  while (query.next())
  {
    participantColumns.insert(query.value(1).toString());
  }
  const bool hasTrainerColumn = participantColumns.remove("is_trainer");
  const QSet<QString> expectedParticipantColumns = {
      "id",         "display_name", "birth_day",  "birth_month",
      "birth_year", "notes",        "rank",       "full_name",
      "contact",    "created_at",   "updated_at", "archived_at"};
  if (participantColumns != expectedParticipantColumns)
  {
    setError("Schema v6 participant columns are unknown");
    return false;
  }
  if (!participantSql.contains(
          "full_name TEXT NOT NULL DEFAULT '' CHECK(length(full_name) <= 300 "
          "AND instr(full_name, char(10)) = 0 AND "
          "instr(full_name, char(13)) = 0)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "contact TEXT NOT NULL DEFAULT '' CHECK(length(contact) <= 500 AND "
          "instr(contact, char(10)) = 0 AND "
          "instr(contact, char(13)) = 0)",
          Qt::CaseInsensitive))
  {
    setError("Schema v6 participant constraints are unknown");
    return false;
  }
  const bool oldMarkerMask =
      markerSql.contains("kind_mask BETWEEN 1 AND 15", Qt::CaseInsensitive);
  const bool currentMarkerMask =
      markerSql.contains("kind_mask BETWEEN 1 AND 31", Qt::CaseInsensitive);
  if (hasTrainerColumn &&
      (!participantSql.contains("is_trainer INTEGER NOT NULL DEFAULT 0",
                                Qt::CaseInsensitive) ||
       !participantSql.contains("is_trainer IN (0, 1)", Qt::CaseInsensitive)))
  {
    setError("Schema v6 trainer constraints are unknown");
    return false;
  }
  if (!oldMarkerMask && !currentMarkerMask)
  {
    setError("Schema v6 day-marker constraints are unknown");
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

  if (hasTrainerColumn &&
      (!query.exec(
           "CREATE TABLE IF NOT EXISTS legacy_v6_participant_trainer_flags("
           "participant_id TEXT PRIMARY KEY NOT NULL, is_trainer INTEGER "
           "NOT NULL CHECK(is_trainer IN (0, 1)))") ||
       !query.exec("INSERT OR REPLACE INTO legacy_v6_participant_trainer_flags("
                   "participant_id, is_trainer) SELECT id, is_trainer FROM "
                   "participants")))
  {
    return fail(query.lastError().text());
  }
  if (!query.exec("DROP TRIGGER participants_profile_insert") ||
      !query.exec("DROP TRIGGER participants_profile_update"))
  {
    return fail(query.lastError().text());
  }
  if (hasTrainerColumn &&
      !query.exec("ALTER TABLE participants DROP COLUMN is_trainer"))
  {
    return fail(query.lastError().text());
  }
  if (oldMarkerMask && !upgradeDayMarkerKindsSchema())
  {
    db_.rollback();
    return false;
  }
  if (!createProfileValidationTriggers())
  {
    db_.rollback();
    return false;
  }
  if (!query.exec("PRAGMA foreign_key_check") || query.next())
  {
    return fail("Development schema v6 repair failed foreign-key check");
  }
  query.finish();
  if (!query.exec(QString("PRAGMA user_version = %1")
                      .arg(kParticipantDetailsSchemaVersion)))
  {
    return fail(query.lastError().text());
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV7ToV8()
{
  if (!verifySchemaV7())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createParticipantNameSchema() ||
      !query.exec(QString("PRAGMA user_version = %1")
                      .arg(kParticipantNameSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v7 to v8 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV8ToV9()
{
  if (!verifySchemaV8())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createCombatHandSchema() ||
      !query.exec(QString("PRAGMA user_version = %1")
                      .arg(kCombatHandSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v8 to v9 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::migrateSchemaV9ToV10()
{
  if (!verifySchemaV9())
  {
    return false;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return false;
  }
  QSqlQuery query(db_);
  if (!createTrainingStartSchema() ||
      !query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Cannot finish schema v9 to v10 migration");
    }
    return false;
  }
  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV3()
{
  const QStringList requiredTables = {"participants", "month_participants",
                                      "attendance", "month_days"};
  for (const QString& table : requiredTables)
  {
    if (!tableExists(table))
    {
      setError(QString("Schema v3 misses table: %1").arg(table));
      return false;
    }
  }

  const QList<QPair<QString, QSet<QString>>> schema = {
      {"participants",
       {"id", "display_name", "birth_day", "birth_month", "birth_year", "notes",
        "created_at", "updated_at", "archived_at"}},
      {"month_participants", {"year", "month", "participant_id", "sort_order"}},
      {"attendance", {"year", "month", "day", "participant_id", "is_checked"}},
      {"month_days", {"year", "month", "day"}}};
  for (const auto& entry : schema)
  {
    if (!tableHasColumns(db_, entry.first, entry.second))
    {
      setError(
          QString("Schema v3 columns are incomplete: %1").arg(entry.first));
      return false;
    }
  }
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA foreign_key_check") || query.next())
  {
    setError("Schema v3 foreign key check failed");
    return false;
  }
  if (!query.exec("SELECT count(*) FROM sqlite_master WHERE type = 'trigger' "
                  "AND name IN ('participants_profile_insert', "
                  "'participants_profile_update')") ||
      !query.next() || query.value(0).toInt() != 2)
  {
    setError("Schema v3 profile validation triggers are missing");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV4()
{
  if (!verifySchemaV3())
  {
    return false;
  }
  if (!tableExists("participant_day_markers") ||
      !tableHasColumns(
          db_, "participant_day_markers",
          {"year", "month", "day", "participant_id", "kind_mask", "note"}))
  {
    setError("Schema v4 day-marker table is missing or incomplete");
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("PRAGMA foreign_key_check") || query.next())
  {
    setError("Schema v4 foreign key check failed");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV5()
{
  if (!verifySchemaV4() || !tableHasColumns(db_, "participants", {"rank"}))
  {
    if (lastError_.isEmpty())
    {
      setError("Schema v5 participant rank column is missing");
    }
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT 1 FROM participants WHERE rank NOT IN "
                  "('page', 'squire', 'novice', 'recruit', 'guest', "
                  "'knight') LIMIT 1") ||
      query.next())
  {
    setError("Schema v5 contains invalid participant ranks");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV7()
{
  if (tableHasColumns(db_, "participants", {"is_trainer"}))
  {
    setError("Schema v7 contains obsolete participant trainer column");
    return false;
  }
  if (!verifySchemaV5() ||
      !tableHasColumns(db_, "participants", {"full_name", "contact"}))
  {
    if (lastError_.isEmpty())
    {
      setError("Schema v7 participant detail columns are missing");
    }
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participants'") ||
      !query.next())
  {
    setError("Schema v7 participant definition is missing");
    return false;
  }
  const QString participantSql = query.value(0).toString().simplified();
  if (!query.exec("PRAGMA table_info(participants)"))
  {
    setError("Cannot read schema v7 participant columns");
    return false;
  }
  QSet<QString> participantColumns;
  while (query.next())
  {
    participantColumns.insert(query.value(1).toString());
  }
  participantColumns.remove("historical_name");
  participantColumns.remove("combat_hand");
  participantColumns.remove("training_start_year");
  participantColumns.remove("training_start_month");
  const QSet<QString> expectedParticipantColumns = {
      "id",         "display_name", "birth_day",  "birth_month",
      "birth_year", "notes",        "rank",       "full_name",
      "contact",    "created_at",   "updated_at", "archived_at"};
  if (participantColumns != expectedParticipantColumns)
  {
    setError("Schema v7 participant columns are incomplete");
    return false;
  }
  if (!participantSql.contains(
          "full_name TEXT NOT NULL DEFAULT '' CHECK(length(full_name) <= 300 "
          "AND instr(full_name, char(10)) = 0 AND "
          "instr(full_name, char(13)) = 0)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "contact TEXT NOT NULL DEFAULT '' CHECK(length(contact) <= 500 AND "
          "instr(contact, char(10)) = 0 AND "
          "instr(contact, char(13)) = 0)",
          Qt::CaseInsensitive))
  {
    setError("Schema v7 participant constraints are incomplete");
    return false;
  }
  if (!query.exec("SELECT 1 FROM participants WHERE length(full_name) > 300 OR "
                  "instr(full_name, char(10)) != 0 OR "
                  "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
                  "instr(contact, char(10)) != 0 OR "
                  "instr(contact, char(13)) != 0 LIMIT 1") ||
      query.next())
  {
    setError("Schema v7 contains invalid participant details");
    return false;
  }
  if (!query.exec("SELECT 1 FROM participant_day_markers WHERE "
                  "typeof(kind_mask) != 'integer' OR kind_mask NOT BETWEEN 1 "
                  "AND 31 LIMIT 1") ||
      query.next())
  {
    setError("Schema v7 contains invalid participant day markers");
    return false;
  }
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participant_day_markers'") ||
      !query.next())
  {
    setError("Schema v7 day-marker definition is missing");
    return false;
  }
  const QString markerSql = query.value(0).toString().simplified();
  if (!markerSql.contains("kind_mask BETWEEN 1 AND 31", Qt::CaseInsensitive) ||
      !markerSql.contains("REFERENCES month_participants",
                          Qt::CaseInsensitive) ||
      !markerSql.contains("ON DELETE CASCADE", Qt::CaseInsensitive))
  {
    setError("Schema v7 day-marker constraints are obsolete");
    return false;
  }
  if (!query.exec("SELECT 1 FROM sqlite_master WHERE type = 'index' AND "
                  "name = 'idx_day_markers_history'") ||
      !query.next())
  {
    setError("Schema v7 day-marker history index is missing");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV8()
{
  if (!verifySchemaV7() ||
      !tableHasColumns(db_, "participants", {"historical_name"}))
  {
    if (lastError_.isEmpty())
    {
      setError("Schema v8 historical-name column is missing");
    }
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participants'") ||
      !query.next())
  {
    setError("Schema v8 participant definition is missing");
    return false;
  }
  const QString participantSql = query.value(0).toString().simplified();
  if (!participantSql.contains("historical_name TEXT NOT NULL DEFAULT '' "
                               "CHECK(length(historical_name) <= 200 AND "
                               "instr(historical_name, char(10)) = 0 AND "
                               "instr(historical_name, char(13)) = 0)",
                               Qt::CaseInsensitive))
  {
    setError("Schema v8 historical-name constraints are incomplete");
    return false;
  }
  if (!participantSql.contains("display_name TEXT NOT NULL DEFAULT ' ' "
                               "CHECK(length(display_name) BETWEEN 1 AND 300)",
                               Qt::CaseInsensitive))
  {
    setError("Schema v8 display-name constraints are incomplete");
    return false;
  }
  if (!query.exec(
          "SELECT 1 FROM participants WHERE length(display_name) > 300 OR "
          "length(trim(display_name)) = 0 OR "
          "length(historical_name) > 200 OR "
          "instr(historical_name, char(10)) != 0 OR "
          "instr(historical_name, char(13)) != 0 OR "
          "(length(trim(historical_name)) = 0 AND "
          "length(trim(full_name)) = 0) OR trim(display_name) != CASE WHEN "
          "length(trim(historical_name)) > 0 THEN trim(historical_name) "
          "ELSE trim(full_name) END LIMIT 1") ||
      query.next())
  {
    setError("Schema v8 contains inconsistent participant names");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV9()
{
  if (!verifySchemaV8() ||
      !tableHasColumns(db_, "participants", {"combat_hand"}))
  {
    if (lastError_.isEmpty())
    {
      setError("Schema v9 combat-hand column is missing");
    }
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participants'") ||
      !query.next())
  {
    setError("Schema v9 participant definition is missing");
    return false;
  }
  const QString participantSql = query.value(0).toString().simplified();
  if (!participantSql.contains(
          "combat_hand TEXT NOT NULL DEFAULT 'unknown' "
          "CHECK(combat_hand IN ('unknown', 'right', 'left'))",
          Qt::CaseInsensitive))
  {
    setError("Schema v9 combat-hand constraints are incomplete");
    return false;
  }
  if (!query.exec("SELECT 1 FROM participants WHERE combat_hand NOT IN "
                  "('unknown', 'right', 'left') LIMIT 1") ||
      query.next())
  {
    setError("Schema v9 contains invalid combat-hand values");
    return false;
  }
  return true;
}

bool SqliteConnect::verifySchemaV10()
{
  if (!verifySchemaV9() ||
      !tableHasColumns(db_, "participants",
                       {"training_start_year", "training_start_month"}))
  {
    if (lastError_.isEmpty())
    {
      setError("Schema v10 training-start columns are missing");
    }
    return false;
  }
  QSqlQuery query(db_);
  if (!query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                  "name = 'participants'") ||
      !query.next())
  {
    setError("Schema v10 participant definition is missing");
    return false;
  }
  const QString participantSql = query.value(0).toString().simplified();
  if (!participantSql.contains(
          "training_start_year INTEGER CHECK(training_start_year BETWEEN "
          "1900 AND 9999)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "training_start_month INTEGER CHECK(training_start_month BETWEEN "
          "1 AND 12)",
          Qt::CaseInsensitive))
  {
    setError("Schema v10 training-start constraints are incomplete");
    return false;
  }
  if (!query.exec(
          "SELECT 1 FROM participants WHERE "
          "(training_start_year IS NULL) != "
          "(training_start_month IS NULL) OR "
          "(training_start_year IS NOT NULL AND "
          "(training_start_year NOT BETWEEN 1900 AND 9999 OR "
          "training_start_month NOT BETWEEN 1 AND 12)) LIMIT 1") ||
      query.next())
  {
    setError("Schema v10 contains invalid training-start months");
    return false;
  }
  if (!query.exec(
          "SELECT sql FROM sqlite_master WHERE type = 'trigger' AND "
          "name IN ('participants_profile_insert', "
          "'participants_profile_update')"))
  {
    setError("Cannot read schema v10 profile validation triggers");
    return false;
  }
  int triggerCount = 0;
  while (query.next())
  {
    const QString triggerSql = query.value(0).toString().simplified();
    if (!triggerSql.contains("NEW.training_start_year IS NULL",
                             Qt::CaseInsensitive) ||
        !triggerSql.contains("NEW.training_start_month IS NULL",
                             Qt::CaseInsensitive))
    {
      setError("Schema v10 training-start validation trigger is obsolete");
      return false;
    }
    ++triggerCount;
  }
  if (triggerCount != 2)
  {
    setError("Schema v10 profile validation triggers are incomplete");
    return false;
  }
  return true;
}

bool SqliteConnect::validateYearMonth(int year, int month) const
{
  return QDate(year, month, 1).isValid();
}

int SqliteConnect::daysInMonth(int year, int month) const
{
  return QDate(year, month, 1).daysInMonth();
}

QVector<int> SqliteConnect::fullMonthDays(int year, int month) const
{
  QVector<int> days;
  const int count = daysInMonth(year, month);
  days.reserve(count);
  for (int day = 1; day <= count; ++day)
  {
    days.push_back(day);
  }
  return days;
}

QVector<int> SqliteConnect::normalizeDays(int year, int month,
                                          const QVector<int>& days) const
{
  QSet<int> unique;
  const int maxDay = daysInMonth(year, month);
  for (int day : days)
  {
    if (day >= 1 && day <= maxDay)
    {
      unique.insert(day);
    }
  }
  QVector<int> result(unique.begin(), unique.end());
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<Participant> SqliteConnect::getParticipantsForMonth(int year,
                                                                int month)
{
  lastError_.clear();
  std::vector<Participant> result;
  QSqlQuery query(db_);
  query.prepare("SELECT p.id, p.display_name, p.historical_name, "
                "p.full_name FROM "
                "month_participants mp "
                "JOIN participants p ON p.id = mp.participant_id "
                "WHERE mp.year = :year AND mp.month = :month "
                "ORDER BY mp.sort_order, p.id");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return result;
  }
  while (query.next())
  {
    result.push_back({{query.value(0).toString()},
                      query.value(1).toString(),
                      query.value(2).toString(),
                      query.value(3).toString()});
  }
  return result;
}

QVector<int> SqliteConnect::getActiveDays(int year, int month)
{
  lastError_.clear();
  QVector<int> days;
  QSqlQuery query(db_);
  query.prepare("SELECT day FROM month_days WHERE year = :year AND "
                "month = :month ORDER BY day");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return days;
  }
  while (query.next())
  {
    days.push_back(query.value(0).toInt());
  }
  return days.isEmpty() ? fullMonthDays(year, month) : days;
}

bool SqliteConnect::saveActiveDays(int year, int month,
                                   const QVector<int>& days)
{
  const QVector<int> normalized = normalizeDays(year, month, days);
  if (!validateYearMonth(year, month) || normalized.isEmpty())
  {
    setError("Invalid or empty active day set");
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
  query.prepare("DELETE FROM month_days WHERE year = :year AND month = :month");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare(
      "INSERT INTO month_days(year, month, day) VALUES(:year, :month, :day)");
  for (int day : normalized)
  {
    query.bindValue(":year", year);
    query.bindValue(":month", month);
    query.bindValue(":day", day);
    if (!query.exec())
    {
      return fail(query.lastError().text());
    }
  }
  query.prepare(
      "INSERT OR IGNORE INTO attendance(year, month, day, participant_id, "
      "is_checked) SELECT :year, :month, :day, participant_id, 0 FROM "
      "month_participants WHERE year = :year AND month = :month");
  for (int day : normalized)
  {
    query.bindValue(":year", year);
    query.bindValue(":month", month);
    query.bindValue(":day", day);
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

std::vector<AttendanceRecord> SqliteConnect::getMonth(int year, int month)
{
  lastError_.clear();
  std::vector<AttendanceRecord> result;
  QSqlQuery query(db_);
  query.prepare(
      "SELECT participant_id, day, is_checked FROM attendance "
      "WHERE year = :year AND month = :month ORDER BY participant_id, day");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return result;
  }
  while (query.next())
  {
    result.push_back({{query.value(0).toString()},
                      query.value(1).toInt(),
                      query.value(2).toInt() != 0});
  }
  return result;
}

bool SqliteConnect::saveAttendance(int year, int month,
                                   const std::vector<AttendanceRecord>& data)
{
  lastError_.clear();
  if (!validateYearMonth(year, month))
  {
    setError("Invalid year or month");
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
  query.prepare(
      "INSERT INTO attendance(year, month, day, participant_id, is_checked) "
      "VALUES(:year, :month, :day, :id, :checked) "
      "ON CONFLICT(year, month, day, participant_id) DO UPDATE SET "
      "is_checked = excluded.is_checked");
  for (const AttendanceRecord& record : data)
  {
    if (!record.participantId.isValid() ||
        !QDate(year, month, record.day).isValid())
    {
      return fail("Invalid attendance record");
    }
    query.bindValue(":year", year);
    query.bindValue(":month", month);
    query.bindValue(":day", record.day);
    query.bindValue(":id", record.participantId.value);
    query.bindValue(":checked", record.isChecked ? 1 : 0);
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

std::vector<ParticipantDayMarker> SqliteConnect::getDayMarkers(int year,
                                                               int month)
{
  lastError_.clear();
  std::vector<ParticipantDayMarker> result;
  if (!validateYearMonth(year, month))
  {
    setError("Invalid year or month");
    return result;
  }

  QSqlQuery query(db_);
  query.prepare("SELECT participant_id, day, kind_mask, note FROM "
                "participant_day_markers WHERE year = :year AND month = :month "
                "ORDER BY participant_id, day");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return result;
  }
  while (query.next())
  {
    const int kindMask = query.value(2).toInt();
    const auto kinds = DayMarkerKindsFromInt(kindMask);
    ParticipantDayMarker marker{{query.value(0).toString()},
                                query.value(1).toInt(),
                                kinds.value_or(DayMarkerKinds()),
                                query.value(3).toString()};
    if (!kinds.has_value() || !marker.participantId.isValid() ||
        !QDate(year, month, marker.day).isValid() ||
        !IsValidDayMarkerKinds(marker.kinds) ||
        marker.note.size() > kMaxDayMarkerNoteLength)
    {
      result.clear();
      setError("Stored participant day marker is invalid");
      return result;
    }
    result.push_back(std::move(marker));
  }
  return result;
}

bool SqliteConnect::saveDayMarker(int year, int month,
                                  const ParticipantDayMarker& marker)
{
  lastError_.clear();
  if (!validateYearMonth(year, month) || !marker.participantId.isValid() ||
      !QDate(year, month, marker.day).isValid() ||
      !IsValidDayMarkerKinds(marker.kinds) ||
      marker.note.size() > kMaxDayMarkerNoteLength)
  {
    setError("Invalid participant day marker");
    return false;
  }

  QSqlQuery query(db_);
  query.prepare(
      "INSERT INTO participant_day_markers(year, month, day, "
      "participant_id, kind_mask, note) VALUES(:year, :month, :day, :id, "
      ":kind_mask, :note) ON CONFLICT(year, month, day, participant_id) DO "
      "UPDATE SET kind_mask = excluded.kind_mask, note = excluded.note");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  query.bindValue(":day", marker.day);
  query.bindValue(":id", marker.participantId.value);
  query.bindValue(":kind_mask", marker.kinds.toInt());
  query.bindValue(":note", marker.note);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  return true;
}

bool SqliteConnect::removeDayMarker(int year, int month,
                                    const ParticipantId& participantId, int day)
{
  lastError_.clear();
  if (!validateYearMonth(year, month) || !participantId.isValid() ||
      !QDate(year, month, day).isValid())
  {
    setError("Invalid participant day marker key");
    return false;
  }
  QSqlQuery query(db_);
  query.prepare("DELETE FROM participant_day_markers WHERE year = :year AND "
                "month = :month AND day = :day AND participant_id = :id");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  query.bindValue(":day", day);
  query.bindValue(":id", participantId.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  return true;
}

bool SqliteConnect::addParticipantToMonth(int year, int month,
                                          const ParticipantProfile& profile)
{
  ParticipantProfile normalized = profile;
  normalized.historicalName = normalized.historicalName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  normalized.displayName = ParticipantDisplayName(normalized);
  if (normalized.historicalName.isNull())
  {
    normalized.historicalName = QStringLiteral("");
  }
  if (normalized.fullName.isNull())
  {
    normalized.fullName = QStringLiteral("");
  }
  if (normalized.contact.isNull())
  {
    normalized.contact = QStringLiteral("");
  }
  if (normalized.notes.isNull())
  {
    normalized.notes = QStringLiteral("");
  }
  if (!validateYearMonth(year, month) ||
      (normalized.historicalName.isEmpty() && normalized.fullName.isEmpty()) ||
      !normalized.isValid() ||
      !IsTrainingStartMonthNotAfter(normalized.trainingStartMonth,
                                    QDate::currentDate()) ||
      normalized.archived)
  {
    setError("Invalid participant or month");
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
  query.prepare(
      "INSERT INTO participants(id, display_name, historical_name, "
      "full_name, contact, birth_day, birth_month, birth_year, rank, "
      "combat_hand, training_start_year, training_start_month, notes) "
      "VALUES(:id, :name, :historical_name, :full_name, :contact, :day, "
      ":month, :year, :rank, :combat_hand, :training_start_year, "
      ":training_start_month, :notes) "
      "ON CONFLICT(id) DO NOTHING");
  query.bindValue(":id", normalized.id.value);
  query.bindValue(":name", normalized.displayName);
  query.bindValue(":historical_name", normalized.historicalName);
  query.bindValue(":full_name", normalized.fullName);
  query.bindValue(":contact", normalized.contact);
  query.bindValue(":rank", ParticipantRankStorageValue(normalized.rank));
  query.bindValue(":combat_hand",
                  CombatHandStorageValue(normalized.combatHand));
  query.bindValue(
      ":training_start_year",
      normalized.trainingStartMonth.has_value()
          ? QVariant(normalized.trainingStartMonth->year)
          : QVariant());
  query.bindValue(
      ":training_start_month",
      normalized.trainingStartMonth.has_value()
          ? QVariant(normalized.trainingStartMonth->month)
          : QVariant());
  query.bindValue(":notes", normalized.notes);
  if (normalized.birthday.has_value())
  {
    query.bindValue(":day", normalized.birthday->day);
    query.bindValue(":month", normalized.birthday->month);
    query.bindValue(":year", normalized.birthday->year.has_value()
                                 ? QVariant(*normalized.birthday->year)
                                 : QVariant());
  }
  else
  {
    query.bindValue(":day", QVariant());
    query.bindValue(":month", QVariant());
    query.bindValue(":year", QVariant());
  }
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare(
      "INSERT OR IGNORE INTO month_participants(year, month, participant_id, "
      "sort_order) VALUES(:year, :month, :id, "
      "COALESCE((SELECT MAX(sort_order) + 1 FROM month_participants WHERE "
      "year = :year AND month = :month), 0))");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  query.bindValue(":id", normalized.id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("SELECT 1 FROM month_days WHERE year = :year AND "
                "month = :month LIMIT 1");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  if (!query.next())
  {
    query.prepare(
        "INSERT INTO month_days(year, month, day) VALUES(:year, :month, :day)");
    for (int day : fullMonthDays(year, month))
    {
      query.bindValue(":year", year);
      query.bindValue(":month", month);
      query.bindValue(":day", day);
      if (!query.exec())
      {
        return fail(query.lastError().text());
      }
    }
  }
  query.prepare(
      "INSERT OR IGNORE INTO attendance(year, month, day, participant_id, "
      "is_checked) VALUES(:year, :month, :day, :id, 0)");
  for (int day : getActiveDays(year, month))
  {
    query.bindValue(":year", year);
    query.bindValue(":month", month);
    query.bindValue(":day", day);
    query.bindValue(":id", normalized.id.value);
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

bool SqliteConnect::removeParticipantFromMonth(int year, int month,
                                               const ParticipantId& id)
{
  if (!validateYearMonth(year, month) || !id.isValid())
  {
    setError("Invalid participant or month");
    return false;
  }
  QSqlQuery query(db_);
  query.prepare("DELETE FROM month_participants WHERE year = :year AND "
                "month = :month AND participant_id = :id");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  return query.numRowsAffected() == 1;
}

bool SqliteConnect::validateSnapshot(int year, int month,
                                     const MonthSnapshot& snapshot)
{
  if (!validateYearMonth(year, month) || snapshot.activeDays.isEmpty())
  {
    setError("Invalid month snapshot");
    return false;
  }
  QSet<QString> ids;
  for (const Participant& participant : snapshot.participants)
  {
    if (!IsValidParticipantSnapshot(participant) ||
        ids.contains(participant.id.value))
    {
      setError("Invalid or duplicate participant in snapshot");
      return false;
    }
    ids.insert(participant.id.value);
  }
  if (normalizeDays(year, month, snapshot.activeDays).size() !=
      snapshot.activeDays.size())
  {
    setError("Invalid active days in snapshot");
    return false;
  }
  QSet<QString> attendanceKeys;
  for (const AttendanceRecord& record : snapshot.attendance)
  {
    const QString key =
        record.participantId.value + ':' + QString::number(record.day);
    if (!ids.contains(record.participantId.value) ||
        !QDate(year, month, record.day).isValid() ||
        attendanceKeys.contains(key))
    {
      setError("Invalid or duplicate attendance in snapshot");
      return false;
    }
    attendanceKeys.insert(key);
  }
  QSet<QString> markerKeys;
  for (const ParticipantDayMarker& marker : snapshot.dayMarkers)
  {
    const QString key =
        marker.participantId.value + ':' + QString::number(marker.day);
    if (!ids.contains(marker.participantId.value) ||
        !QDate(year, month, marker.day).isValid() ||
        !IsValidDayMarkerKinds(marker.kinds) ||
        marker.note.size() > kMaxDayMarkerNoteLength ||
        markerKeys.contains(key))
    {
      setError("Invalid or duplicate day marker in snapshot");
      return false;
    }
    markerKeys.insert(key);
  }
  return true;
}

bool SqliteConnect::replaceMonth(int year, int month,
                                 const MonthSnapshot& snapshot)
{
  if (!validateSnapshot(year, month, snapshot) || !db_.transaction())
  {
    if (lastError_.isEmpty())
    {
      setError(db_.lastError().text());
    }
    return false;
  }
  auto fail = [this](const QString& error)
  {
    db_.rollback();
    setError(error);
    return false;
  };
  QSqlQuery query(db_);
  query.prepare("DELETE FROM participant_day_markers WHERE year = :year AND "
                "month = :month");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("DELETE FROM attendance WHERE year = :year AND month = :month");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("DELETE FROM month_participants WHERE year = :year AND "
                "month = :month");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  query.prepare("DELETE FROM month_days WHERE year = :year AND month = :month");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }

  QSqlQuery participantQuery(db_);
  participantQuery.prepare(
      "INSERT INTO participants(id, display_name, historical_name, "
      "full_name) VALUES(:id, :name, :historical_name, :full_name) "
      "ON CONFLICT(id) DO NOTHING");
  QSqlQuery membershipQuery(db_);
  membershipQuery.prepare(
      "INSERT INTO month_participants(year, month, participant_id, "
      "sort_order) VALUES(:year, :month, :id, :sort)");
  int sortOrder = 0;
  for (const Participant& participant : snapshot.participants)
  {
    participantQuery.bindValue(":id", participant.id.value);
    participantQuery.bindValue(":name", participant.displayName);
    participantQuery.bindValue(":historical_name",
                               participant.historicalName.isNull()
                                   ? QStringLiteral("")
                                   : participant.historicalName);
    participantQuery.bindValue(":full_name", participant.fullName.isNull()
                                                 ? QStringLiteral("")
                                                 : participant.fullName);
    if (!participantQuery.exec())
    {
      return fail(participantQuery.lastError().text());
    }
    membershipQuery.bindValue(":year", year);
    membershipQuery.bindValue(":month", month);
    membershipQuery.bindValue(":id", participant.id.value);
    membershipQuery.bindValue(":sort", sortOrder++);
    if (!membershipQuery.exec())
    {
      return fail(membershipQuery.lastError().text());
    }
  }
  QSqlQuery dayQuery(db_);
  dayQuery.prepare(
      "INSERT INTO month_days(year, month, day) VALUES(:year, :month, :day)");
  for (int day : snapshot.activeDays)
  {
    dayQuery.bindValue(":year", year);
    dayQuery.bindValue(":month", month);
    dayQuery.bindValue(":day", day);
    if (!dayQuery.exec())
    {
      return fail(dayQuery.lastError().text());
    }
  }
  QSqlQuery attendanceQuery(db_);
  attendanceQuery.prepare(
      "INSERT INTO attendance(year, month, day, participant_id, is_checked) "
      "VALUES(:year, :month, :day, :id, :checked)");
  for (const AttendanceRecord& record : snapshot.attendance)
  {
    attendanceQuery.bindValue(":year", year);
    attendanceQuery.bindValue(":month", month);
    attendanceQuery.bindValue(":day", record.day);
    attendanceQuery.bindValue(":id", record.participantId.value);
    attendanceQuery.bindValue(":checked", record.isChecked ? 1 : 0);
    if (!attendanceQuery.exec())
    {
      return fail(attendanceQuery.lastError().text());
    }
  }
  QSqlQuery markerQuery(db_);
  markerQuery.prepare(
      "INSERT INTO participant_day_markers(year, month, day, "
      "participant_id, kind_mask, note) VALUES(:year, :month, :day, :id, "
      ":kind_mask, :note)");
  for (const ParticipantDayMarker& marker : snapshot.dayMarkers)
  {
    markerQuery.bindValue(":year", year);
    markerQuery.bindValue(":month", month);
    markerQuery.bindValue(":day", marker.day);
    markerQuery.bindValue(":id", marker.participantId.value);
    markerQuery.bindValue(":kind_mask", marker.kinds.toInt());
    markerQuery.bindValue(":note", marker.note);
    if (!markerQuery.exec())
    {
      return fail(markerQuery.lastError().text());
    }
  }
  if (!db_.commit())
  {
    return fail(db_.lastError().text());
  }
  return true;
}
std::optional<ParticipantProfile>
SqliteConnect::getParticipantProfile(const ParticipantId& id)
{
  if (!id.isValid())
  {
    setError("Invalid participant ID");
    return std::nullopt;
  }
  QSqlQuery query(db_);
  query.prepare(
      "SELECT id, display_name, birth_day, birth_month, birth_year, rank, "
      "notes, archived_at IS NOT NULL, full_name, contact, historical_name, "
      "combat_hand, training_start_year, training_start_month "
      "FROM participants WHERE id = :id");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  if (!query.next())
  {
    return std::nullopt;
  }

  ParticipantProfile profile;
  profile.id = {query.value(0).toString()};
  profile.displayName = query.value(1).toString();
  const bool hasDay = !query.value(2).isNull();
  const bool hasMonth = !query.value(3).isNull();
  const bool hasYear = !query.value(4).isNull();
  if (hasDay != hasMonth || (hasYear && !hasDay))
  {
    setError("Stored participant birthday columns are inconsistent");
    return std::nullopt;
  }
  if (hasDay)
  {
    Birthday birthday{query.value(2).toInt(), query.value(3).toInt(),
                      std::nullopt};
    if (!query.value(4).isNull())
    {
      birthday.year = query.value(4).toInt();
    }
    profile.birthday = birthday;
  }
  const auto rank = ParticipantRankFromStorageValue(query.value(5).toString());
  if (!rank.has_value())
  {
    setError("Stored participant rank is invalid");
    return std::nullopt;
  }
  profile.rank = *rank;
  profile.notes = query.value(6).toString();
  profile.archived = query.value(7).toInt() != 0;
  profile.fullName = query.value(8).toString();
  profile.contact = query.value(9).toString();
  profile.historicalName = query.value(10).toString();
  const auto combatHand =
      CombatHandFromStorageValue(query.value(11).toString());
  if (!combatHand.has_value())
  {
    setError("Stored participant combat hand is invalid");
    return std::nullopt;
  }
  profile.combatHand = *combatHand;
  const bool hasTrainingStartYear = !query.value(12).isNull();
  const bool hasTrainingStartMonth = !query.value(13).isNull();
  if (hasTrainingStartYear != hasTrainingStartMonth)
  {
    setError("Stored participant training-start columns are inconsistent");
    return std::nullopt;
  }
  if (hasTrainingStartYear)
  {
    profile.trainingStartMonth =
        JournalMonth{query.value(12).toInt(), query.value(13).toInt()};
  }
  if (!profile.isValid())
  {
    setError("Stored participant profile is invalid");
    return std::nullopt;
  }
  return profile;
}

std::optional<ParticipantJournalStatistics>
SqliteConnect::participantStatistics(const ParticipantId& id)
{
  lastError_.clear();
  if (!id.isValid())
  {
    setError("Invalid participant ID");
    return std::nullopt;
  }
  if (!db_.transaction())
  {
    setError(db_.lastError().text());
    return std::nullopt;
  }
  auto fail = [this](const QString& error)
      -> std::optional<ParticipantJournalStatistics>
  {
    db_.rollback();
    setError(error);
    return std::nullopt;
  };
  auto monthKey = [](int year, int month)
  {
    return static_cast<qint64>(year) * 100 + month;
  };
  auto dateKey = [&monthKey](int year, int month, int day)
  {
    return monthKey(year, month) * 100 + day;
  };

  QSqlQuery query(db_);
  query.prepare("SELECT EXISTS(SELECT 1 FROM participants WHERE id = :id)");
  query.bindValue(":id", id.value);
  if (!query.exec() || !query.next())
  {
    return fail(query.lastError().text());
  }
  if (!query.value(0).toBool())
  {
    return fail("Participant not found");
  }
  query.finish();

  ParticipantJournalStatistics result{id, {}, 0, 0, 0, std::nullopt,
                                      std::nullopt};
  QHash<qint64, int> monthIndexes;
  query.prepare("SELECT year, month FROM month_participants "
                "WHERE participant_id = :id ORDER BY year, month");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  while (query.next())
  {
    const int year = query.value(0).toInt();
    const int month = query.value(1).toInt();
    const qint64 key = monthKey(year, month);
    if (!QDate(year, month, 1).isValid() || monthIndexes.contains(key))
    {
      return fail("Stored participant month is invalid");
    }
    monthIndexes.insert(key, static_cast<int>(result.months.size()));
    result.months.push_back({{year, month}, 0, 0, 0, 0});
  }
  query.finish();

  QHash<qint64, QSet<int>> activeDaysByMonth;
  query.prepare(
      "SELECT md.year, md.month, md.day FROM month_days md "
      "JOIN month_participants mp ON mp.year = md.year AND "
      "mp.month = md.month WHERE mp.participant_id = :id "
      "ORDER BY md.year, md.month, md.day");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  while (query.next())
  {
    const int year = query.value(0).toInt();
    const int month = query.value(1).toInt();
    const int day = query.value(2).toInt();
    const qint64 key = monthKey(year, month);
    QSet<int>& days = activeDaysByMonth[key];
    if (!monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        days.contains(day))
    {
      return fail("Stored participant active day is invalid");
    }
    days.insert(day);
  }
  query.finish();
  for (ParticipantMonthStatistics& month : result.months)
  {
    const qint64 key = monthKey(month.month.year, month.month.month);
    const QSet<int> days = activeDaysByMonth.value(key);
    month.trackedDayCount =
        days.isEmpty()
            ? QDate(month.month.year, month.month.month, 1).daysInMonth()
            : days.size();
  }

  QSet<qint64> checkedDays;
  query.prepare("SELECT year, month, day FROM attendance "
                "WHERE participant_id = :id AND is_checked = 1 "
                "ORDER BY year, month, day");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  while (query.next())
  {
    const int year = query.value(0).toInt();
    const int month = query.value(1).toInt();
    const int day = query.value(2).toInt();
    const qint64 key = monthKey(year, month);
    const qint64 checkedKey = dateKey(year, month, day);
    const QSet<int> activeDays = activeDaysByMonth.value(key);
    if (!monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        checkedDays.contains(checkedKey))
    {
      return fail("Stored participant attendance is invalid");
    }
    if (!activeDays.isEmpty() && !activeDays.contains(day))
    {
      continue;
    }
    checkedDays.insert(checkedKey);
    ParticipantMonthStatistics& statistics =
        result.months.at(monthIndexes.value(key));
    ++statistics.attendedDayCount;
    ++result.totalAttendedDayCount;
    const QDate date(year, month, day);
    if (!result.firstRecordedVisit.has_value() ||
        date < *result.firstRecordedVisit)
    {
      result.firstRecordedVisit = date;
    }
    if (!result.lastRecordedVisit.has_value() ||
        date > *result.lastRecordedVisit)
    {
      result.lastRecordedVisit = date;
    }
  }
  query.finish();

  QSet<qint64> markerDays;
  query.prepare("SELECT year, month, day, kind_mask FROM "
                "participant_day_markers WHERE participant_id = :id "
                "ORDER BY year, month, day");
  query.bindValue(":id", id.value);
  if (!query.exec())
  {
    return fail(query.lastError().text());
  }
  while (query.next())
  {
    const int year = query.value(0).toInt();
    const int month = query.value(1).toInt();
    const int day = query.value(2).toInt();
    const auto kinds = DayMarkerKindsFromInt(query.value(3).toInt());
    const qint64 key = monthKey(year, month);
    const qint64 markerKey = dateKey(year, month, day);
    const QSet<int> activeDays = activeDaysByMonth.value(key);
    if (!monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        !kinds.has_value() || markerDays.contains(markerKey))
    {
      return fail("Stored participant day marker is invalid");
    }
    markerDays.insert(markerKey);
    if (!activeDays.isEmpty() && !activeDays.contains(day))
    {
      continue;
    }
    ParticipantMonthStatistics& statistics =
        result.months.at(monthIndexes.value(key));
    if (kinds->testFlag(DayMarkerKind::SpecialTraining) &&
        checkedDays.contains(markerKey))
    {
      ++statistics.specialTrainingVisitCount;
      ++result.totalSpecialTrainingVisitCount;
    }
    if (kinds->testFlag(DayMarkerKind::LedTraining))
    {
      ++statistics.ledTrainingDayCount;
      ++result.totalLedTrainingDayCount;
    }
  }
  query.finish();

  if (!db_.commit())
  {
    const QString error = db_.lastError().text();
    db_.rollback();
    setError(error);
    return std::nullopt;
  }
  return result;
}

std::optional<std::vector<ParticipantProfile>>
SqliteConnect::listParticipantProfiles(bool includeArchived)
{
  std::vector<ParticipantProfile> result;
  QSqlQuery query(db_);
  const QString sql =
      "SELECT id FROM participants " +
      QString(includeArchived ? "" : "WHERE archived_at IS NULL ") +
      "ORDER BY CASE rank WHEN 'page' THEN 0 WHEN 'squire' THEN 1 "
      "WHEN 'novice' THEN 2 WHEN 'recruit' THEN 3 WHEN 'guest' THEN 4 "
      "WHEN 'knight' THEN 5 ELSE 6 END, lower(display_name), id";
  if (!query.exec(sql))
  {
    setError(query.lastError().text());
    return std::nullopt;
  }
  QVector<ParticipantId> ids;
  while (query.next())
  {
    ids.push_back({query.value(0).toString()});
  }
  for (const ParticipantId& id : ids)
  {
    const auto profile = getParticipantProfile(id);
    if (!profile.has_value())
    {
      return std::nullopt;
    }
    result.push_back(*profile);
  }
  return result;
}

bool SqliteConnect::updateParticipantProfile(const ParticipantProfile& profile)
{
  ParticipantProfile normalized = profile;
  normalized.historicalName = normalized.historicalName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  normalized.displayName = ParticipantDisplayName(normalized);
  if (normalized.historicalName.isNull())
  {
    normalized.historicalName = QStringLiteral("");
  }
  if (normalized.fullName.isNull())
  {
    normalized.fullName = QStringLiteral("");
  }
  if (normalized.contact.isNull())
  {
    normalized.contact = QStringLiteral("");
  }
  if (normalized.notes.isNull())
  {
    normalized.notes = QStringLiteral("");
  }
  if ((normalized.historicalName.isEmpty() && normalized.fullName.isEmpty()) ||
      !normalized.isValid() ||
      !IsTrainingStartMonthNotAfter(normalized.trainingStartMonth,
                                    QDate::currentDate()))
  {
    setError("Invalid participant profile");
    return false;
  }

  QSqlQuery query(db_);
  query.prepare(
      "UPDATE participants SET display_name = :name, birth_day = :day, "
      "birth_month = :month, birth_year = :year, rank = :rank, "
      "notes = :notes, historical_name = :historical_name, "
      "full_name = :full_name, contact = :contact, "
      "combat_hand = :combat_hand, training_start_year = "
      ":training_start_year, training_start_month = :training_start_month, "
      "updated_at = "
      "CURRENT_TIMESTAMP WHERE id = :id");
  query.bindValue(":id", normalized.id.value);
  query.bindValue(":name", normalized.displayName);
  query.bindValue(":rank", ParticipantRankStorageValue(normalized.rank));
  query.bindValue(":notes", normalized.notes);
  query.bindValue(":historical_name", normalized.historicalName);
  query.bindValue(":full_name", normalized.fullName);
  query.bindValue(":contact", normalized.contact);
  query.bindValue(":combat_hand",
                  CombatHandStorageValue(normalized.combatHand));
  query.bindValue(
      ":training_start_year",
      normalized.trainingStartMonth.has_value()
          ? QVariant(normalized.trainingStartMonth->year)
          : QVariant());
  query.bindValue(
      ":training_start_month",
      normalized.trainingStartMonth.has_value()
          ? QVariant(normalized.trainingStartMonth->month)
          : QVariant());
  if (normalized.birthday.has_value())
  {
    query.bindValue(":day", normalized.birthday->day);
    query.bindValue(":month", normalized.birthday->month);
    query.bindValue(":year", normalized.birthday->year.has_value()
                                 ? QVariant(*normalized.birthday->year)
                                 : QVariant());
  }
  else
  {
    query.bindValue(":day", QVariant());
    query.bindValue(":month", QVariant());
    query.bindValue(":year", QVariant());
  }
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  return query.numRowsAffected() == 1;
}

bool SqliteConnect::setParticipantArchived(const ParticipantId& id,
                                           bool archived)
{
  if (!id.isValid())
  {
    setError("Invalid participant ID");
    return false;
  }
  QSqlQuery query(db_);
  query.prepare(
      "UPDATE participants SET archived_at = CASE WHEN :archived = 1 THEN "
      "COALESCE(archived_at, CURRENT_TIMESTAMP) ELSE NULL END, "
      "updated_at = CURRENT_TIMESTAMP WHERE id = :id");
  query.bindValue(":id", id.value);
  query.bindValue(":archived", archived ? 1 : 0);
  if (!query.exec())
  {
    setError(query.lastError().text());
    return false;
  }
  return query.numRowsAffected() == 1;
}
