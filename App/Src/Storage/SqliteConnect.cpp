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

constexpr int kSchemaVersion = 2;

bool isUuid(const ParticipantId& id)
{
  return id.value.size() == 36 && !QUuid(id.value).isNull();
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

  return enableForeignKeys() && ensureSchema();
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
    const QStringList required = {"participants", "month_participants",
                                  "attendance", "month_days"};
    for (const QString& table : required)
    {
      if (!tableExists(table))
      {
        setError(QString("Schema v2 misses table: %1").arg(table));
        return false;
      }
    }
    QSqlQuery integrity(db_);
    if (!integrity.exec("PRAGMA foreign_key_check") || integrity.next())
    {
      setError("Schema v2 foreign key check failed");
      return false;
    }
    if (!integrity.exec("PRAGMA foreign_key_list(attendance)") ||
        !integrity.next())
    {
      setError("Schema v2 attendance foreign key is missing");
      return false;
    }
    return true;
  }

  const QStringList incompatible = {"users", "participants",
                                    "month_participants", "attendance",
                                    "month_days"};
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
  if (!createSchemaV2())
  {
    db_.rollback();
    return false;
  }
  if (!query.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion)) ||
      !query.exec("PRAGMA foreign_key_check") || query.next())
  {
    db_.rollback();
    setError("Schema v2 creation verification failed");
    return false;
  }
  if (!db_.commit())
  {
    setError(db_.lastError().text());
    return false;
  }
  return true;
}
bool SqliteConnect::createSchemaV2()
{
  QSqlQuery query(db_);
  const QStringList statements = {
      "CREATE TABLE participants ("
      "id TEXT PRIMARY KEY NOT NULL, "
      "display_name TEXT NOT NULL CHECK(length(display_name) > 0), "
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
  query.prepare("SELECT p.id, p.display_name FROM month_participants mp "
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
  query.prepare(
      "INSERT INTO participants(id, display_name) VALUES(:id, :name) "
      "ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, "
      "updated_at = CURRENT_TIMESTAMP");
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
