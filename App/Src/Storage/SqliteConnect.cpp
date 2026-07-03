#include "SqliteConnect.hpp"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QUuid>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>

SqliteConnect::SqliteConnect() = default;

//---------------------------------------------------------------

SqliteConnect::~SqliteConnect() {
  // Порядок важен:
  // 1) закрыть db_;
  // 2) сбросить handle db_ в пустой QSqlDatabase;
  // 3) удалить зарегистрированное имя подключения из глобального пула Qt.
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
  // Важно: имя подключения должно быть уникальным для каждого объекта.
  // Иначе временный SqliteConnect (например, в Pull/Read Base) может удалить
  // общее connection и сломать активное рабочее подключение окна.
  if (connectionName_.isEmpty()) {
    connectionName_ = QString("journal_connection_%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  }
  db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);

  db_.setDatabaseName(dbPath);
  if (!db_.open()) {
    qWarning() << "SqliteConnect::open failed:" << db_.lastError().text();
    return false;
  }

  // После успешного open сразу гарантируем наличие актуальной схемы.
  return ensureSchema();
}

//---------------------------------------------------------------

bool SqliteConnect::ensureSchema() {
  QSqlQuery query(db_);
  if (!query.exec("CREATE TABLE IF NOT EXISTS people ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "display_name TEXT NOT NULL UNIQUE, "
                  "age INTEGER, "
                  "profile_url TEXT, "
                  "notes TEXT)")) {
    return false;
  }

  if (!query.exec("CREATE TABLE IF NOT EXISTS person_photos ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "person_id INTEGER NOT NULL, "
                  "file_path TEXT NOT NULL, "
                  "sort_order INTEGER NOT NULL DEFAULT 0, "
                  "FOREIGN KEY(person_id) REFERENCES people(id) ON DELETE CASCADE)")) {
    return false;
  }

  if (!query.exec("CREATE TABLE IF NOT EXISTS attendance ("
                  "person_id INTEGER NOT NULL, "
                  "date TEXT NOT NULL, "
                  "is_checked INTEGER NOT NULL, "
                  "PRIMARY KEY(person_id, date), "
                  "FOREIGN KEY(person_id) REFERENCES people(id) ON DELETE CASCADE)")) {
    return false;
  }

  if (!query.exec("CREATE TABLE IF NOT EXISTS month_days ("
                  "year INTEGER NOT NULL, "
                  "month INTEGER NOT NULL, "
                  "day INTEGER NOT NULL, "
                  "PRIMARY KEY(year, month, day))")) {
    return false;
  }

  return migrateLegacyUsers();
}

//---------------------------------------------------------------

bool SqliteConnect::migrateLegacyUsers() {
  QSqlQuery tableCheck(db_);
  tableCheck.prepare(
      "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'users' LIMIT 1");
  if (!tableCheck.exec()) {
    qWarning() << "SqliteConnect::migrateLegacyUsers table check failed:"
               << tableCheck.lastError().text();
    return false;
  }
  if (!tableCheck.next()) {
    return true;
  }

  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::migrateLegacyUsers transaction start failed";
    return false;
  }

  QSqlQuery insertPeople(db_);
  if (!insertPeople.exec(
          "INSERT OR IGNORE INTO people(display_name) "
          "SELECT DISTINCT name FROM users WHERE TRIM(name) <> ''")) {
    qWarning() << "SqliteConnect::migrateLegacyUsers people insert failed:"
               << insertPeople.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery insertAttendance(db_);
  if (!insertAttendance.exec(
          "INSERT OR REPLACE INTO attendance(person_id, date, is_checked) "
          "SELECT people.id, users.date, users.is_checked "
          "FROM users JOIN people ON people.display_name = users.name")) {
    qWarning() << "SqliteConnect::migrateLegacyUsers attendance insert failed:"
               << insertAttendance.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery legacyCheck(db_);
  legacyCheck.prepare(
      "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'users_legacy' LIMIT 1");
  if (!legacyCheck.exec()) {
    qWarning() << "SqliteConnect::migrateLegacyUsers legacy check failed:"
               << legacyCheck.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery archiveQuery(db_);
  const bool archiveOk = legacyCheck.next()
                             ? archiveQuery.exec("DROP TABLE users")
                             : archiveQuery.exec("ALTER TABLE users RENAME TO users_legacy");
  if (!archiveOk) {
    qWarning() << "SqliteConnect::migrateLegacyUsers archive failed:"
               << archiveQuery.lastError().text();
    db_.rollback();
    return false;
  }

  return db_.commit();
}

//---------------------------------------------------------------

int SqliteConnect::findPersonId(const QString& name) {
  QSqlQuery query(db_);
  query.prepare("SELECT id FROM people WHERE display_name = :name LIMIT 1");
  query.bindValue(":name", name.trimmed());
  if (!query.exec()) {
    qWarning() << "SqliteConnect::findPersonId query failed:" << query.lastError().text();
    return 0;
  }
  return query.next() ? query.value(0).toInt() : 0;
}

//---------------------------------------------------------------

int SqliteConnect::ensurePerson(const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return 0;
  }

  QSqlQuery insertQuery(db_);
  insertQuery.prepare("INSERT OR IGNORE INTO people(display_name) VALUES(:name)");
  insertQuery.bindValue(":name", trimmed);
  if (!insertQuery.exec()) {
    qWarning() << "SqliteConnect::ensurePerson insert failed:" << insertQuery.lastError().text();
    return 0;
  }

  return findPersonId(trimmed);
}

//---------------------------------------------------------------

QVector<int> SqliteConnect::fullMonthDays(int year, int month) const {
  QVector<int> days;
  const int maxDay = daysInMonth(year, month);
  days.reserve(maxDay);
  for (int day = 1; day <= maxDay; ++day) {
    days.push_back(day);
  }
  return days;
}

//---------------------------------------------------------------

QVector<int> SqliteConnect::normalizeDays(int year, int month,
                                          const QVector<int>& days) const {
  QSet<int> uniqueDays;
  const int maxDay = daysInMonth(year, month);
  for (int day : days) {
    if (day >= 1 && day <= maxDay) {
      uniqueDays.insert(day);
    }
  }

  QVector<int> normalized;
  normalized.reserve(uniqueDays.size());
  for (int day : uniqueDays) {
    normalized.push_back(day);
  }
  std::sort(normalized.begin(), normalized.end());
  return normalized;
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
  query.prepare(
      "SELECT DISTINCT people.display_name "
      "FROM people "
      "JOIN attendance ON attendance.person_id = people.id "
      "WHERE attendance.date LIKE :month "
      "ORDER BY people.display_name ASC");
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

QVector<int> SqliteConnect::getActiveDays(int year, int month) {
  QVector<int> days;

  QSqlQuery query(db_);
  query.prepare(
      "SELECT day FROM month_days WHERE year = :year AND month = :month ORDER BY day ASC");
  query.bindValue(":year", year);
  query.bindValue(":month", month);

  if (!query.exec()) {
    qWarning() << "SqliteConnect::getActiveDays query failed:" << query.lastError().text();
    return fullMonthDays(year, month);
  }

  while (query.next()) {
    days.push_back(query.value(0).toInt());
  }

  // Пустая настройка трактуется как отсутствие настройки: старое поведение.
  if (days.isEmpty()) {
    return fullMonthDays(year, month);
  }

  return normalizeDays(year, month, days);
}

//---------------------------------------------------------------

bool SqliteConnect::saveActiveDays(int year, int month, const QVector<int>& days) {
  const QVector<int> normalizedDays = normalizeDays(year, month, days);
  if (normalizedDays.isEmpty()) {
    qWarning() << "SqliteConnect::saveActiveDays rejected empty days list";
    return false;
  }

  const QStringList users = getUsersForMonth(year, month);

  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::saveActiveDays transaction start failed";
    return false;
  }

  QSqlQuery deleteConfigQuery(db_);
  deleteConfigQuery.prepare("DELETE FROM month_days WHERE year = :year AND month = :month");
  deleteConfigQuery.bindValue(":year", year);
  deleteConfigQuery.bindValue(":month", month);
  if (!deleteConfigQuery.exec()) {
    qWarning() << "SqliteConnect::saveActiveDays config delete failed:"
               << deleteConfigQuery.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery insertConfigQuery(db_);
  insertConfigQuery.prepare(
      "INSERT INTO month_days(year, month, day) VALUES(:year, :month, :day)");
  for (int day : normalizedDays) {
    insertConfigQuery.bindValue(":year", year);
    insertConfigQuery.bindValue(":month", month);
    insertConfigQuery.bindValue(":day", day);
    if (!insertConfigQuery.exec()) {
      qWarning() << "SqliteConnect::saveActiveDays config insert failed:"
                 << insertConfigQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }

  QStringList activeDayNumbers;
  for (int day : normalizedDays) {
    activeDayNumbers.push_back(QString::number(day));
  }

  QSqlQuery deleteInactiveQuery(db_);
  deleteInactiveQuery.prepare(
      QString("DELETE FROM attendance WHERE date LIKE :month AND "
              "CAST(substr(date, 1, 2) AS INTEGER) NOT IN (%1)")
          .arg(activeDayNumbers.join(", ")));
  deleteInactiveQuery.bindValue(":month", monthPattern(year, month));
  if (!deleteInactiveQuery.exec()) {
    qWarning() << "SqliteConnect::saveActiveDays inactive delete failed:"
               << deleteInactiveQuery.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery existsQuery(db_);
  QSqlQuery insertAttendanceQuery(db_);
  insertAttendanceQuery.prepare(
      "INSERT OR IGNORE INTO attendance(person_id, date, is_checked) "
      "VALUES(:person_id, :date, 0)");

  for (const QString& user : users) {
    const int personId = ensurePerson(user);
    if (personId == 0) {
      db_.rollback();
      return false;
    }

    for (int day : normalizedDays) {
      existsQuery.prepare(
          "SELECT 1 FROM attendance WHERE person_id = :person_id AND date = :date LIMIT 1");
      existsQuery.bindValue(":person_id", personId);
      existsQuery.bindValue(":date", dayString(year, month, day));
      if (!existsQuery.exec()) {
        qWarning() << "SqliteConnect::saveActiveDays exists check failed:"
                   << existsQuery.lastError().text();
        db_.rollback();
        return false;
      }

      if (existsQuery.next()) {
        continue;
      }

      insertAttendanceQuery.bindValue(":person_id", personId);
      insertAttendanceQuery.bindValue(":date", dayString(year, month, day));
      if (!insertAttendanceQuery.exec()) {
        qWarning() << "SqliteConnect::saveActiveDays attendance insert failed:"
                   << insertAttendanceQuery.lastError().text();
        db_.rollback();
        return false;
      }
    }
  }

  return db_.commit();
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> SqliteConnect::getMonth(int year, int month) {
  std::vector<AttendanceRecord> records;

  QSqlQuery query(db_);
  query.prepare(
      "SELECT people.display_name, attendance.date, attendance.is_checked "
      "FROM attendance "
      "JOIN people ON people.id = attendance.person_id "
      "WHERE attendance.date LIKE :month "
      "ORDER BY people.display_name ASC, attendance.date ASC");
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

  // Полная перезапись месяца в транзакции:
  // delete за месяц + insert всех записей из UI-среза.
  // Такой подход удобен для PoC и повторяемого состояния.
  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::saveMonth transaction start failed";
    return false;
  }

  QSqlQuery deleteQuery(db_);
  deleteQuery.prepare("DELETE FROM attendance WHERE date LIKE :month");
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
      "INSERT OR REPLACE INTO attendance(person_id, date, is_checked) "
      "VALUES(:person_id, :date, :checked)");

  QElapsedTimer insertTimer;
  insertTimer.start();
  for (const AttendanceRecord& record : data) {
    const int personId = ensurePerson(record.userName);
    if (personId == 0) {
      qWarning() << "SqliteConnect::saveMonth ensurePerson failed:" << record.userName;
      db_.rollback();
      return false;
    }

    insertQuery.bindValue(":person_id", personId);
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
  if (!committed) {
    qWarning() << "SqliteConnect::saveMonth commit failed:" << db_.lastError().text();
  }
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

  // Проверяем существование по месяцу, потому что один пользователь хранится многими строками.
  QSqlQuery existsQuery(db_);
  const int personId = ensurePerson(trimmed);
  if (personId == 0) {
    return false;
  }

  existsQuery.prepare(
      "SELECT 1 FROM attendance WHERE person_id = :person_id AND date LIKE :month LIMIT 1");
  existsQuery.bindValue(":person_id", personId);
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
      "INSERT INTO attendance(person_id, date, is_checked) VALUES(:person_id, :date, :checked)");

  // При добавлении создаем записи на каждый день месяца.
  const QVector<int> activeDays = getActiveDays(year, month);
  for (int day : activeDays) {
    insertQuery.bindValue(":person_id", personId);
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

  // Одним DELETE убираем сразу все строки пользователя за выбранный месяц.
  QSqlQuery query(db_);
  const int personId = findPersonId(trimmed);
  if (personId == 0) {
    return false;
  }

  query.prepare("DELETE FROM attendance WHERE person_id = :person_id AND date LIKE :month");
  query.bindValue(":person_id", personId);
  query.bindValue(":month", monthPattern(year, month));

  const bool ok = query.exec();
  if (!ok) {
    qWarning() << "SqliteConnect::deleteUser delete failed:" << query.lastError().text();
  }
  return ok;
}

//---------------------------------------------------------------

bool SqliteConnect::getPersonProfile(const QString& name, PersonProfile* profile) {
  if (!profile) {
    return false;
  }

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  if (ensurePerson(trimmed) == 0) {
    return false;
  }

  QSqlQuery query(db_);
  query.prepare(
      "SELECT display_name, age, profile_url, notes "
      "FROM people WHERE display_name = :name LIMIT 1");
  query.bindValue(":name", trimmed);
  if (!query.exec()) {
    qWarning() << "SqliteConnect::getPersonProfile query failed:"
               << query.lastError().text();
    return false;
  }
  if (!query.next()) {
    return false;
  }

  profile->displayName = query.value(0).toString();
  profile->age = query.value(1).isNull() ? 0 : query.value(1).toInt();
  profile->profileUrl = query.value(2).toString();
  profile->notes = query.value(3).toString();
  return true;
}

//---------------------------------------------------------------

bool SqliteConnect::updatePersonProfile(const QString& originalName,
                                        const PersonProfile& profile) {
  const QString oldName = originalName.trimmed();
  const QString newName = profile.displayName.trimmed();
  if (oldName.isEmpty() || newName.isEmpty()) {
    return false;
  }

  const int personId = ensurePerson(oldName);
  if (personId == 0) {
    return false;
  }

  QSqlQuery duplicateQuery(db_);
  duplicateQuery.prepare(
      "SELECT 1 FROM people WHERE display_name = :name AND id <> :id LIMIT 1");
  duplicateQuery.bindValue(":name", newName);
  duplicateQuery.bindValue(":id", personId);
  if (!duplicateQuery.exec()) {
    qWarning() << "SqliteConnect::updatePersonProfile duplicate check failed:"
               << duplicateQuery.lastError().text();
    return false;
  }
  if (duplicateQuery.next()) {
    return false;
  }

  QSqlQuery updateQuery(db_);
  updateQuery.prepare(
      "UPDATE people "
      "SET display_name = :name, age = :age, profile_url = :profile_url, notes = :notes "
      "WHERE id = :id");
  updateQuery.bindValue(":name", newName);
  if (profile.age > 0) {
    updateQuery.bindValue(":age", profile.age);
  } else {
    updateQuery.bindValue(":age", QVariant());
  }
  updateQuery.bindValue(":profile_url", profile.profileUrl.trimmed());
  updateQuery.bindValue(":notes", profile.notes.trimmed());
  updateQuery.bindValue(":id", personId);

  const bool ok = updateQuery.exec();
  if (!ok) {
    qWarning() << "SqliteConnect::updatePersonProfile update failed:"
               << updateQuery.lastError().text();
  }
  return ok;
}

//---------------------------------------------------------------
