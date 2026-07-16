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

constexpr int kSchemaVersion = 3;

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
  if (version > kSchemaVersion)
  {
    setError(QString("Unsupported SQLite schema version: %1").arg(version));
    return false;
  }
  if (version == kSchemaVersion)
  {
    return verifySchemaV3();
  }
  if (version == 2)
  {
    return migrateSchemaV2ToV3() && verifySchemaV3();
  }
  if (version != 0)
  {
    setError(QString("Unsupported SQLite schema version: %1").arg(version));
    return false;
  }

  if (tableExists("users"))
  {
    return migrateLegacyUsersToV3() && verifySchemaV3();
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
  if (!createSchemaV3() ||
      !query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    if (lastError_.isEmpty())
    {
      setError("Schema v3 creation verification failed");
    }
    return false;
  }
  if (!db_.commit())
  {
    setError(db_.lastError().text());
    return false;
  }
  return verifySchemaV3();
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

  if (!query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)) ||
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
  const QString validation =
      "length(trim(NEW.display_name)) = 0 OR "
      "length(NEW.display_name) > 200 OR length(NEW.notes) > 4096 OR NOT ("
      "(NEW.birth_day IS NULL AND NEW.birth_month IS NULL AND "
      "NEW.birth_year IS NULL) OR (NEW.birth_day IS NOT NULL AND "
      "NEW.birth_month IS NOT NULL AND NEW.birth_day <= CASE "
      "NEW.birth_month WHEN 2 THEN CASE WHEN COALESCE(NEW.birth_year, 2000) "
      "% 400 = 0 OR (COALESCE(NEW.birth_year, 2000) % 4 = 0 AND "
      "COALESCE(NEW.birth_year, 2000) % 100 != 0) THEN 29 ELSE 28 END "
      "WHEN 4 THEN 30 WHEN 6 THEN 30 WHEN 9 THEN 30 WHEN 11 THEN 30 "
      "ELSE 31 END))";
  QSqlQuery query(db_);
  const QStringList statements = {
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(validation),
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(validation)};
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
      !query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)))
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
  std::vector<Participant> result;
  QSqlQuery query(db_);
  query.prepare("SELECT p.id, p.display_name FROM "
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
    result.push_back({{query.value(0).toString()}, query.value(1).toString()});
  }
  return result;
}

QVector<int> SqliteConnect::getActiveDays(int year, int month)
{
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
  return db_.commit();
}

std::vector<AttendanceRecord> SqliteConnect::getMonth(int year, int month)
{
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
  return db_.commit();
}

bool SqliteConnect::addParticipantToMonth(int year, int month,
                                          const Participant& participant)
{
  if (!validateYearMonth(year, month) || !participant.id.isValid() ||
      participant.displayName.isEmpty())
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
  query.prepare("INSERT INTO participants(id, display_name) VALUES(:id, :name) "
                "ON CONFLICT(id) DO NOTHING");
  query.bindValue(":id", participant.id.value);
  query.bindValue(":name", participant.displayName);
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
  query.bindValue(":id", participant.id.value);
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
    query.bindValue(":id", participant.id.value);
    if (!query.exec())
    {
      return fail(query.lastError().text());
    }
  }
  return db_.commit();
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
    if (!participant.id.isValid() || participant.displayName.isEmpty() ||
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
      "INSERT INTO participants(id, display_name) VALUES(:id, :name) "
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
  return db_.commit();
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
      "SELECT id, display_name, birth_day, birth_month, birth_year, notes, "
      "archived_at IS NOT NULL FROM participants WHERE id = :id");
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
  profile.notes = query.value(5).toString();
  profile.archived = query.value(6).toInt() != 0;
  if (!profile.isValid())
  {
    setError("Stored participant profile is invalid");
    return std::nullopt;
  }
  return profile;
}

std::optional<std::vector<ParticipantProfile>>
SqliteConnect::listParticipantProfiles(bool includeArchived)
{
  std::vector<ParticipantProfile> result;
  QSqlQuery query(db_);
  const QString sql =
      "SELECT id FROM participants " +
      QString(includeArchived ? "" : "WHERE archived_at IS NULL ") +
      "ORDER BY lower(display_name), id";
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
  normalized.displayName = normalized.displayName.trimmed();
  if (!normalized.isValid())
  {
    setError("Invalid participant profile");
    return false;
  }

  QSqlQuery query(db_);
  query.prepare(
      "UPDATE participants SET display_name = :name, birth_day = :day, "
      "birth_month = :month, birth_year = :year, notes = :notes, "
      "updated_at = CURRENT_TIMESTAMP WHERE id = :id");
  query.bindValue(":id", normalized.id.value);
  query.bindValue(":name", normalized.displayName);
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
