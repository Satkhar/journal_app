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

QString SqliteConnect::lastError() const {
  return lastError_;
}

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
  lastError_.clear();
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
    lastError_ = db_.lastError().text();
    qWarning() << "SqliteConnect::open failed:" << lastError_;
    return false;
  }

  // После успешного open сразу гарантируем наличие таблицы users.
  const bool schemaReady = ensureSchema();
  if (!schemaReady && lastError_.isEmpty()) {
    lastError_ = "Не удалось подготовить схему SQLite";
  }
  return schemaReady;
}

//---------------------------------------------------------------

bool SqliteConnect::ensureSchema() {
  QSqlQuery query(db_);
  // Нормализованная схема: одна запись на (user, day).
  if (!query.exec("CREATE TABLE IF NOT EXISTS users ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL, "
                  "date TEXT NOT NULL, "
                  "is_checked INTEGER NOT NULL)")) {
    return false;
  }

  if (!query.exec("CREATE TABLE IF NOT EXISTS month_days ("
                  "year INTEGER NOT NULL, "
                  "month INTEGER NOT NULL, "
                  "day INTEGER NOT NULL, "
                  "PRIMARY KEY(year, month, day))")) {
    return false;
  }

  // Отдельный marker отличает созданный месяц без пользователей от нового.
  if (!query.exec("CREATE TABLE IF NOT EXISTS months ("
                  "year INTEGER NOT NULL, "
                  "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
                  "PRIMARY KEY(year, month))")) {
    return false;
  }

  // Год в старом dd.MM утрачен. Mapping фиксирует его один раз и не дает
  // повторно интерпретировать те же строки как новый год при следующем запуске.
  if (!query.exec("CREATE TABLE IF NOT EXISTS legacy_months ("
                  "year INTEGER NOT NULL, "
                  "month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), "
                  "PRIMARY KEY(year, month))")) {
    return false;
  }

  QSqlQuery ambiguousLegacyQuery(db_);
  if (!ambiguousLegacyQuery.exec(
          "SELECT CAST(substr(u.date, 4, 2) AS INTEGER) "
          "FROM users AS u "
          "WHERE length(u.date) = 5 "
          "AND (SELECT COUNT(DISTINCT md.year) FROM month_days AS md "
          "WHERE md.month = CAST(substr(u.date, 4, 2) AS INTEGER)) > 1 "
          "AND NOT EXISTS (SELECT 1 FROM legacy_months AS lm "
          "WHERE lm.month = CAST(substr(u.date, 4, 2) AS INTEGER)) "
          "LIMIT 1")) {
    lastError_ = ambiguousLegacyQuery.lastError().text();
    return false;
  }
  if (ambiguousLegacyQuery.next()) {
    lastError_ =
        "Legacy dd.MM данные соответствуют нескольким годам month_days";
    qWarning() << "SqliteConnect::ensureSchema:" << lastError_;
    return false;
  }

  QSqlQuery legacyMigration(db_);
  legacyMigration.prepare(
      "INSERT OR IGNORE INTO legacy_months(year, month) "
      "SELECT COALESCE(("
      "SELECT CASE WHEN COUNT(DISTINCT md.year) = 1 THEN MIN(md.year) END "
      "FROM month_days AS md "
      "WHERE md.month = CAST(substr(u.date, 4, 2) AS INTEGER)), "
      ":legacy_year), CAST(substr(u.date, 4, 2) AS INTEGER) "
      "FROM users AS u "
      "WHERE length(u.date) = 5 AND substr(u.date, 3, 1) = '.' "
      "AND CAST(substr(u.date, 4, 2) AS INTEGER) BETWEEN 1 AND 12 "
      "AND NOT EXISTS ("
      "SELECT 1 FROM legacy_months AS lm "
      "WHERE lm.month = CAST(substr(u.date, 4, 2) AS INTEGER)) "
      "GROUP BY CAST(substr(u.date, 4, 2) AS INTEGER)");
  legacyMigration.bindValue(":legacy_year", QDate::currentDate().year());
  if (!legacyMigration.exec()) {
    return false;
  }

  return query.exec(
      "INSERT OR IGNORE INTO months(year, month) "
      "SELECT year, month FROM legacy_months");
}

//---------------------------------------------------------------

MonthStateResult SqliteConnect::getMonthState(int year, int month) {
  lastError_.clear();
  if (!QDate(year, month, 1).isValid()) {
    lastError_ = "Некорректные год или месяц";
    return {MonthState::Error, lastError_};
  }

  QSqlQuery markerQuery(db_);
  markerQuery.prepare(
      "SELECT 1 FROM months WHERE year = :year AND month = :month LIMIT 1");
  markerQuery.bindValue(":year", year);
  markerQuery.bindValue(":month", month);
  if (!markerQuery.exec()) {
    lastError_ = markerQuery.lastError().text();
    return {MonthState::Error, lastError_};
  }
  if (markerQuery.next()) {
    return {MonthState::Ready, QString()};
  }

  // month_days существовал до marker-таблицы и сам является явной настройкой.
  QSqlQuery daysQuery(db_);
  daysQuery.prepare(
      "SELECT 1 FROM month_days WHERE year = :year AND month = :month LIMIT 1");
  daysQuery.bindValue(":year", year);
  daysQuery.bindValue(":month", month);
  if (!daysQuery.exec()) {
    lastError_ = daysQuery.lastError().text();
    return {MonthState::Error, lastError_};
  }
  if (daysQuery.next()) {
    return {MonthState::Ready, QString()};
  }

  // Поддержка данных без marker: новые строки фильтруются по году, старые
  // dd.MM доступны только через зафиксированный legacy_months mapping.
  QSqlQuery dataQuery(db_);
  dataQuery.prepare(
      QString("SELECT 1 FROM users WHERE %1 LIMIT 1").arg(monthPredicate()));
  bindMonth(dataQuery, year, month);
  if (!dataQuery.exec()) {
    lastError_ = dataQuery.lastError().text();
    return {MonthState::Error, lastError_};
  }

  return {dataQuery.next() ? MonthState::Ready : MonthState::Missing,
          QString()};
}

//---------------------------------------------------------------

bool SqliteConnect::markMonthInitialized(int year, int month) {
  QSqlQuery query(db_);
  query.prepare(
      "INSERT INTO months(year, month) VALUES(:year, :month) "
      "ON CONFLICT(year, month) DO NOTHING");
  query.bindValue(":year", year);
  query.bindValue(":month", month);
  if (!query.exec()) {
    lastError_ = query.lastError().text();
    qWarning() << "SqliteConnect::markMonthInitialized failed:"
               << lastError_;
    return false;
  }
  return true;
}

//---------------------------------------------------------------

bool SqliteConnect::commitTransaction(const char* operation) {
  if (db_.commit()) {
    return true;
  }

  const QString commitError = db_.lastError().text();
  lastError_ = commitError;
  qWarning() << operation << "commit failed:" << commitError;
  if (!db_.rollback()) {
    qWarning() << operation << "rollback after commit failure failed:"
               << db_.lastError().text();
  }
  return false;
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

QString SqliteConnect::monthPredicate() const {
  return "(date LIKE :month_pattern OR "
         "(date LIKE :legacy_month_pattern AND EXISTS ("
         "SELECT 1 FROM legacy_months "
         "WHERE year = :year AND month = :month_number)))";
}

//---------------------------------------------------------------

void SqliteConnect::bindMonth(QSqlQuery& query, int year, int month) const {
  query.bindValue(":month_pattern", monthPattern(year, month));
  query.bindValue(":legacy_month_pattern", legacyMonthPattern(month));
  query.bindValue(":year", year);
  query.bindValue(":month_number", month);
}

//---------------------------------------------------------------

QString SqliteConnect::monthPattern(int year, int month) const {
  return QString("__.%1.%2")
      .arg(month, 2, 10, QLatin1Char('0'))
      .arg(year, 4, 10, QLatin1Char('0'));
}

//---------------------------------------------------------------

QString SqliteConnect::legacyMonthPattern(int month) const {
  return QString("__.%1").arg(month, 2, 10, QLatin1Char('0'));
}

//---------------------------------------------------------------

QString SqliteConnect::dayString(int year, int month, int day) const {
  return QString("%1.%2.%3")
      .arg(day, 2, 10, QLatin1Char('0'))
      .arg(month, 2, 10, QLatin1Char('0'))
      .arg(year, 4, 10, QLatin1Char('0'));
}

//---------------------------------------------------------------

QString SqliteConnect::legacyDayString(int month, int day) const {
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
  lastError_.clear();
  QStringList users;

  QSqlQuery query(db_);
  query.prepare(QString("SELECT DISTINCT name FROM users WHERE %1 "
                        "ORDER BY name ASC")
                    .arg(monthPredicate()));
  bindMonth(query, year, month);

  if (!query.exec()) {
    lastError_ = query.lastError().text();
    qWarning() << "SqliteConnect::getUsersForMonth query failed:" << lastError_;
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
  lastError_.clear();
  QVector<int> days;

  QSqlQuery query(db_);
  query.prepare(
      "SELECT day FROM month_days WHERE year = :year AND month = :month ORDER BY day ASC");
  query.bindValue(":year", year);
  query.bindValue(":month", month);

  if (!query.exec()) {
    lastError_ = query.lastError().text();
    qWarning() << "SqliteConnect::getActiveDays query failed:" << lastError_;
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
  if (!lastError_.isEmpty()) {
    qWarning() << "SqliteConnect::saveActiveDays users read failed:"
               << lastError_;
    return false;
  }

  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::saveActiveDays transaction start failed";
    return false;
  }

  if (!markMonthInitialized(year, month)) {
    db_.rollback();
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
      QString("DELETE FROM users WHERE %1 AND "
               "CAST(substr(date, 1, 2) AS INTEGER) NOT IN (%2)")
          .arg(monthPredicate(), activeDayNumbers.join(", ")));
  bindMonth(deleteInactiveQuery, year, month);
  if (!deleteInactiveQuery.exec()) {
    qWarning() << "SqliteConnect::saveActiveDays inactive delete failed:"
               << deleteInactiveQuery.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery existsQuery(db_);
  QSqlQuery insertAttendanceQuery(db_);
  insertAttendanceQuery.prepare(
      "INSERT INTO users(name, date, is_checked) VALUES(:name, :date, 0)");

  for (const QString& user : users) {
    for (int day : normalizedDays) {
      existsQuery.prepare(
          "SELECT 1 FROM users WHERE name = :name AND "
          "(date = :date OR (date = :legacy_date AND EXISTS ("
          "SELECT 1 FROM legacy_months "
          "WHERE year = :year AND month = :month_number))) LIMIT 1");
      existsQuery.bindValue(":name", user);
      existsQuery.bindValue(":date", dayString(year, month, day));
      existsQuery.bindValue(":legacy_date", legacyDayString(month, day));
      existsQuery.bindValue(":year", year);
      existsQuery.bindValue(":month_number", month);
      if (!existsQuery.exec()) {
        qWarning() << "SqliteConnect::saveActiveDays exists check failed:"
                   << existsQuery.lastError().text();
        db_.rollback();
        return false;
      }

      if (existsQuery.next()) {
        continue;
      }

      insertAttendanceQuery.bindValue(":name", user);
      insertAttendanceQuery.bindValue(":date", dayString(year, month, day));
      if (!insertAttendanceQuery.exec()) {
        qWarning() << "SqliteConnect::saveActiveDays attendance insert failed:"
                   << insertAttendanceQuery.lastError().text();
        db_.rollback();
        return false;
      }
    }
  }

  return commitTransaction("SqliteConnect::saveActiveDays");
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> SqliteConnect::getMonth(int year, int month) {
  lastError_.clear();
  std::vector<AttendanceRecord> records;

  QSqlQuery query(db_);
  query.prepare(QString("SELECT name, date, is_checked FROM users WHERE %1 "
                        "ORDER BY name ASC, date ASC")
                    .arg(monthPredicate()));
  bindMonth(query, year, month);

  if (!query.exec()) {
    lastError_ = query.lastError().text();
    qWarning() << "SqliteConnect::getMonth query failed:" << lastError_;
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

  if (!markMonthInitialized(year, month)) {
    db_.rollback();
    return false;
  }

  QSqlQuery deleteQuery(db_);
  deleteQuery.prepare(
      QString("DELETE FROM users WHERE %1").arg(monthPredicate()));
  bindMonth(deleteQuery, year, month);
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
  const bool committed = commitTransaction("SqliteConnect::saveMonth");
  qInfo() << "SqliteConnect::saveMonth commit stage ms:" << commitTimer.elapsed();
  qInfo() << "SqliteConnect::saveMonth total ms:" << totalTimer.elapsed();
  return committed;
}

//---------------------------------------------------------------

bool SqliteConnect::saveMonthSetup(
    int year, int month, const QVector<int>& days,
    const std::vector<AttendanceRecord>& data) {
  lastError_.clear();
  const QVector<int> normalizedDays = normalizeDays(year, month, days);
  if (normalizedDays.isEmpty()) {
    lastError_ = "Список дней месяца пуст или некорректен";
    return false;
  }

  QSet<int> allowedDays;
  for (int day : normalizedDays) {
    allowedDays.insert(day);
  }

  if (!db_.transaction()) {
    lastError_ = db_.lastError().text();
    qWarning() << "SqliteConnect::saveMonthSetup transaction start failed:"
               << lastError_;
    return false;
  }

  if (!markMonthInitialized(year, month)) {
    db_.rollback();
    return false;
  }

  QSqlQuery deleteDaysQuery(db_);
  deleteDaysQuery.prepare(
      "DELETE FROM month_days WHERE year = :year AND month = :month");
  deleteDaysQuery.bindValue(":year", year);
  deleteDaysQuery.bindValue(":month", month);
  if (!deleteDaysQuery.exec()) {
    lastError_ = deleteDaysQuery.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery insertDayQuery(db_);
  insertDayQuery.prepare(
      "INSERT INTO month_days(year, month, day) "
      "VALUES(:year, :month, :day)");
  for (int day : normalizedDays) {
    insertDayQuery.bindValue(":year", year);
    insertDayQuery.bindValue(":month", month);
    insertDayQuery.bindValue(":day", day);
    if (!insertDayQuery.exec()) {
      lastError_ = insertDayQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }

  QSqlQuery deleteAttendanceQuery(db_);
  deleteAttendanceQuery.prepare(
      QString("DELETE FROM users WHERE %1").arg(monthPredicate()));
  bindMonth(deleteAttendanceQuery, year, month);
  if (!deleteAttendanceQuery.exec()) {
    lastError_ = deleteAttendanceQuery.lastError().text();
    db_.rollback();
    return false;
  }

  QSqlQuery insertAttendanceQuery(db_);
  insertAttendanceQuery.prepare(
      "INSERT INTO users(name, date, is_checked) "
      "VALUES(:name, :date, :checked)");
  for (const AttendanceRecord& record : data) {
    const QString name = record.userName.trimmed();
    if (name.isEmpty() || !allowedDays.contains(record.day)) {
      lastError_ = "Запись посещаемости не соответствует настройке месяца";
      db_.rollback();
      return false;
    }

    insertAttendanceQuery.bindValue(":name", name);
    insertAttendanceQuery.bindValue(
        ":date", dayString(year, month, record.day));
    insertAttendanceQuery.bindValue(":checked", record.isChecked ? 1 : 0);
    if (!insertAttendanceQuery.exec()) {
      lastError_ = insertAttendanceQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }

  return commitTransaction("SqliteConnect::saveMonthSetup");
}

//---------------------------------------------------------------

bool SqliteConnect::addUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  // Проверяем существование по месяцу, потому что один пользователь хранится многими строками.
  QSqlQuery existsQuery(db_);
  existsQuery.prepare(
      QString("SELECT 1 FROM users WHERE name = :name AND %1 LIMIT 1")
          .arg(monthPredicate()));
  existsQuery.bindValue(":name", trimmed);
  bindMonth(existsQuery, year, month);

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

  if (!markMonthInitialized(year, month)) {
    db_.rollback();
    return false;
  }

  QSqlQuery insertQuery(db_);
  insertQuery.prepare(
      "INSERT INTO users(name, date, is_checked) VALUES(:name, :date, :checked)");

  // При добавлении создаем записи на каждый день месяца.
  const QVector<int> activeDays = getActiveDays(year, month);
  if (!lastError_.isEmpty()) {
    qWarning() << "SqliteConnect::addUser active days read failed:"
               << lastError_;
    db_.rollback();
    return false;
  }
  for (int day : activeDays) {
    insertQuery.bindValue(":name", trimmed);
    insertQuery.bindValue(":date", dayString(year, month, day));
    insertQuery.bindValue(":checked", 0);

    if (!insertQuery.exec()) {
      qWarning() << "SqliteConnect::addUser insert failed:" << insertQuery.lastError().text();
      db_.rollback();
      return false;
    }
  }

  return commitTransaction("SqliteConnect::addUser");
}

//---------------------------------------------------------------

bool SqliteConnect::deleteUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  if (!db_.transaction()) {
    qWarning() << "SqliteConnect::deleteUser transaction start failed";
    return false;
  }

  // Одним DELETE убираем сразу все строки пользователя за выбранный месяц.
  QSqlQuery query(db_);
  query.prepare(QString("DELETE FROM users WHERE name = :name AND %1")
                    .arg(monthPredicate()));
  query.bindValue(":name", trimmed);
  bindMonth(query, year, month);

  const bool ok = query.exec();
  if (!ok) {
    qWarning() << "SqliteConnect::deleteUser delete failed:" << query.lastError().text();
    db_.rollback();
    return false;
  }

  // Legacy-месяц мог не иметь marker. После удаления последнего пользователя
  // он остается созданным и не должен снова запускать setup.
  if (query.numRowsAffected() > 0 && !markMonthInitialized(year, month)) {
    db_.rollback();
    return false;
  }

  return commitTransaction("SqliteConnect::deleteUser");
}

//---------------------------------------------------------------
