#include "SqliteConnect.hpp"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtGlobal>

SqliteConnect::SqliteConnect() = default;

//---------------------------------------------------------------

SqliteConnect::~SqliteConnect() {
  if (db_.isOpen()) {
    db_.close();
  }

  if (!connectionName_.isEmpty()) {
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
  }
}

//---------------------------------------------------------------

bool SqliteConnect::open(const QString& dbPath) {
  connectionName_ = "journal_connection";
  if (QSqlDatabase::contains(connectionName_)) {
    db_ = QSqlDatabase::database(connectionName_);
  } else {
    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
  }

  db_.setDatabaseName(dbPath);
  if (!db_.open()) {
    return false;
  }

  return ensureSchema();
}

//---------------------------------------------------------------

bool SqliteConnect::ensureSchema() {
  QSqlQuery query(db_);
  // Нормализованная схема: одна запись на (user, day).
  return query.exec("CREATE TABLE IF NOT EXISTS users ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT NOT NULL, "
                    "date TEXT NOT NULL, "
                    "is_checked INTEGER NOT NULL)");
}

//---------------------------------------------------------------

QString SqliteConnect::monthPattern(int year, int month) const {
  Q_UNUSED(year);
  // Совместимость со старой БД: там дата была в формате dd.MM.
  // Суффикс % позволяет читать и новый формат dd.MM.yyyy, если он встретится.
  return QString("__.%1%").arg(month, 2, 10, QLatin1Char('0'));
}

//---------------------------------------------------------------

QString SqliteConnect::dayString(int year, int month, int day) const {
  Q_UNUSED(year);
  // Пишем в старом формате, чтобы чтение существующей базы было бесшовным.
  return QString("%1.%2")
      .arg(day, 2, 10, QLatin1Char('0'))
      .arg(month, 2, 10, QLatin1Char('0'));
}

//---------------------------------------------------------------

int SqliteConnect::daysInMonth(int year, int month) const {
  return QDate(year, month, 1).daysInMonth();
}

//---------------------------------------------------------------

QStringList SqliteConnect::getUsersForMonth(int year, int month) {
  QStringList users;

  QSqlQuery query(db_);
  query.prepare("SELECT DISTINCT name FROM users WHERE date LIKE :month ORDER BY name ASC");
  query.bindValue(":month", monthPattern(year, month));

  if (!query.exec()) {
    qWarning() << "SqliteConnect::getUsersForMonth query failed:" << query.lastError().text();
    return users;
  }

  // DISTINCT исключает дубли, возникающие из-за хранения по дням.
  while (query.next()) {
    users.push_back(query.value(0).toString());
  }

  return users;
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> SqliteConnect::getMonth(int year, int month) {
  std::vector<AttendanceRecord> records;

  QSqlQuery query(db_);
  query.prepare(
      "SELECT name, date, is_checked FROM users WHERE date LIKE :month ORDER BY name ASC, date ASC");
  query.bindValue(":month", monthPattern(year, month));

  if (!query.exec()) {
    qWarning() << "SqliteConnect::getMonth query failed:" << query.lastError().text();
    return records;
  }

  // Преобразуем dd.MM.yyyy -> day, чтобы UI работал с числом дня.
  while (query.next()) {
    const QString fullDate = query.value(1).toString();
    const int day = fullDate.left(2).toInt();

    records.push_back({query.value(0).toString(), day,
                       query.value(2).toInt() != 0});
  }

  return records;
}

//---------------------------------------------------------------

bool SqliteConnect::saveMonth(int year, int month,
                              const std::vector<AttendanceRecord>& data) {
  QElapsedTimer totalTimer;
  totalTimer.start();

  // Полная перезапись месяца в транзакции: delete + batch insert.
  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::saveMonth transaction start failed";
    return false;
  }

  QSqlQuery deleteQuery(db_);
  deleteQuery.prepare("DELETE FROM users WHERE date LIKE :month");
  deleteQuery.bindValue(":month", monthPattern(year, month));
  QElapsedTimer deleteTimer;
  deleteTimer.start();
  if (!deleteQuery.exec()) {
    qWarning() << "SqliteConnect::saveMonth delete failed:" << deleteQuery.lastError().text();
    db_.rollback();
    return false;
  }
  qInfo() << "SqliteConnect::saveMonth delete stage ms:" << deleteTimer.elapsed();

  QSqlQuery insertQuery(db_);
  insertQuery.prepare(
      "INSERT INTO users(name, date, is_checked) VALUES(:name, :date, :checked)");

  QElapsedTimer insertTimer;
  insertTimer.start();
  for (const AttendanceRecord& record : data) {
    insertQuery.bindValue(":name", record.userName);
    insertQuery.bindValue(":date", dayString(year, month, record.day));
    insertQuery.bindValue(":checked", record.isChecked ? 1 : 0);

    if (!insertQuery.exec()) {
      qWarning() << "SqliteConnect::saveMonth insert failed:" << insertQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }
  qInfo() << "SqliteConnect::saveMonth insert stage ms:" << insertTimer.elapsed();

  QElapsedTimer commitTimer;
  commitTimer.start();
  const bool committed = db_.commit();
  qInfo() << "SqliteConnect::saveMonth commit stage ms:" << commitTimer.elapsed();
  qInfo() << "SqliteConnect::saveMonth total ms:" << totalTimer.elapsed();
  return committed;
}

//---------------------------------------------------------------

bool SqliteConnect::addUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QSqlQuery existsQuery(db_);
  existsQuery.prepare("SELECT 1 FROM users WHERE name = :name AND date LIKE :month LIMIT 1");
  existsQuery.bindValue(":name", trimmed);
  existsQuery.bindValue(":month", monthPattern(year, month));

  if (!existsQuery.exec()) {
    qWarning() << "SqliteConnect::addUser exists check failed:" << existsQuery.lastError().text();
    return false;
  }

  // Если пользователь уже есть в месяце, дубли не создаем.
  if (existsQuery.next()) {
    return false;
  }

  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::addUser transaction start failed";
    return false;
  }

  QSqlQuery insertQuery(db_);
  insertQuery.prepare(
      "INSERT INTO users(name, date, is_checked) VALUES(:name, :date, :checked)");

  // При добавлении создаем записи на каждый день месяца.
  const int maxDay = daysInMonth(year, month);
  for (int day = 1; day <= maxDay; ++day) {
    insertQuery.bindValue(":name", trimmed);
    insertQuery.bindValue(":date", dayString(year, month, day));
    insertQuery.bindValue(":checked", 0);

    if (!insertQuery.exec()) {
      qWarning() << "SqliteConnect::addUser insert failed:" << insertQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }

  return db_.commit();
}

//---------------------------------------------------------------

bool SqliteConnect::deleteUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QSqlQuery query(db_);
  query.prepare("DELETE FROM users WHERE name = :name AND date LIKE :month");
  query.bindValue(":name", trimmed);
  query.bindValue(":month", monthPattern(year, month));

  const bool ok = query.exec();
  if (!ok) {
    qWarning() << "SqliteConnect::deleteUser delete failed:" << query.lastError().text();
  }
  return ok;
}

//---------------------------------------------------------------
