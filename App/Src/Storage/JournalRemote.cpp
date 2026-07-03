#include "JournalRemote.hpp"

#include <QDate>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

JournalRemote::JournalRemote(const QString& baseUrl, int timeoutMs)
    : baseUrl_(baseUrl), timeoutMs_(timeoutMs) {}

bool JournalRemote::connect(QString* errorMessage) {
  // connect() в этом классе не держит persistent socket:
  // это "логический connect" = проверить доступность endpoint + схему.
  lastError_.clear();
  return ensureSchema(errorMessage);
}

QString JournalRemote::lastError() const {
  return lastError_;
}

QStringList JournalRemote::getUsersForMonth(int year, int month) {
  lastError_.clear();
  QStringList users;
  QJsonArray results;

  const QString sql =
      QString("SELECT DISTINCT people.display_name "
              "FROM people "
              "JOIN attendance ON attendance.person_id = people.id "
              "WHERE attendance.date LIKE '%1' "
              "ORDER BY people.display_name ASC")
          .arg(monthPattern(year, month));

  QString error;
  if (!executePipeline({sql}, &results, &error)) {
    lastError_ = error;
    return users;
  }

  if (results.isEmpty()) {
    return users;
  }

  const QJsonObject result = results.at(0).toObject();
  // Формат libsql результата:
  // result.rows = [ [ {"type":"text","value":"Alice"} ], ... ]
  const QJsonArray rows = result.value("rows").toArray();
  for (const QJsonValue& rowValue : rows) {
    const QJsonArray row = rowValue.toArray();
    if (row.isEmpty()) {
      continue;
    }
    users.push_back(row.at(0).toObject().value("value").toString());
  }

  return users;
}

QVector<int> JournalRemote::getActiveDays(int year, int month) {
  QVector<int> days;
  const int maxDay = daysInMonth(year, month);
  days.reserve(maxDay);
  for (int day = 1; day <= maxDay; ++day) {
    days.push_back(day);
  }
  return days;
}

bool JournalRemote::saveActiveDays(int year, int month, const QVector<int>& days) {
  Q_UNUSED(year);
  Q_UNUSED(month);
  Q_UNUSED(days);
  lastError_ = "Remote month day configuration is not implemented";
  return false;
}

std::vector<AttendanceRecord> JournalRemote::getMonth(int year, int month) {
  lastError_.clear();
  std::vector<AttendanceRecord> records;
  QJsonArray results;

  const QString sql = QString(
      "SELECT people.display_name, attendance.date, attendance.is_checked "
      "FROM attendance "
      "JOIN people ON people.id = attendance.person_id "
      "WHERE attendance.date LIKE '%1' "
      "ORDER BY people.display_name ASC, attendance.date ASC")
                          .arg(monthPattern(year, month));

  QString error;
  if (!executePipeline({sql}, &results, &error)) {
    lastError_ = error;
    return records;
  }

  if (results.isEmpty()) {
    return records;
  }

  const QJsonObject result = results.at(0).toObject();
  const QJsonArray rows = result.value("rows").toArray();
  for (const QJsonValue& rowValue : rows) {
    const QJsonArray row = rowValue.toArray();
    if (row.size() < 3) {
      continue;
    }

    const QString userName = row.at(0).toObject().value("value").toString();
    const QString fullDate = row.at(1).toObject().value("value").toString();
    const int day = fullDate.left(2).toInt();
    // libsql может вернуть integer как строку "0"/"1".
    const bool isChecked = row.at(2).toObject().value("value").toString() == "1";

    records.push_back({userName, day, isChecked});
  }

  return records;
}

bool JournalRemote::saveMonth(int year, int month,
                              const std::vector<AttendanceRecord>& data) {
  lastError_.clear();
  QList<QString> statements;
  // Для PoC sync проще поддерживать атомарной полной перезаписью месяца.
  statements.push_back("BEGIN");
  statements.push_back(
      QString("DELETE FROM attendance WHERE date LIKE '%1'").arg(monthPattern(year, month)));

  for (const AttendanceRecord& record : data) {
    statements.push_back(
        QString("INSERT OR IGNORE INTO people(display_name) VALUES(%1)")
            .arg(sqlQuote(record.userName))
    );
    statements.push_back(
        QString("INSERT OR REPLACE INTO attendance(person_id, date, is_checked) "
                "VALUES((SELECT id FROM people WHERE display_name = %1), %2, %3)")
            .arg(sqlQuote(record.userName))
            .arg(sqlQuote(dayString(year, month, record.day)))
            .arg(record.isChecked ? "1" : "0"));
  }
  statements.push_back("COMMIT");

  QJsonArray results;
  QString error;
  if (!executePipeline(statements, &results, &error)) {
    lastError_ = error;
    QList<QString> rollback;
    rollback.push_back("ROLLBACK");
    // Rollback "best effort": если не сработает, ошибка уже зафиксирована выше.
    executePipeline(rollback, nullptr, nullptr);
    return false;
  }

  return true;
}

bool JournalRemote::addUser(int year, int month, const QString& name) {
  lastError_.clear();
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  // Сначала проверяем наличие пользователя, чтобы не удвоить дневные строки месяца.
  QJsonArray results;
  QString error;
  if (!executePipeline(
          {QString(
               "SELECT 1 "
               "FROM attendance "
               "JOIN people ON people.id = attendance.person_id "
               "WHERE people.display_name = %1 AND attendance.date LIKE '%2' LIMIT 1")
               .arg(sqlQuote(trimmed))
               .arg(monthPattern(year, month))},
          &results, &error)) {
    lastError_ = error;
    return false;
  }

  if (!results.isEmpty() &&
      !results.at(0).toObject().value("rows").toArray().isEmpty()) {
    // Пользователь уже существует в этом месяце.
    return false;
  }

  QList<QString> statements;
  statements.push_back("BEGIN");
  statements.push_back(
      QString("INSERT OR IGNORE INTO people(display_name) VALUES(%1)").arg(sqlQuote(trimmed)));
  const int maxDay = daysInMonth(year, month);
  // Как и в local storage, создаем набор строк сразу на все дни месяца.
  for (int day = 1; day <= maxDay; ++day) {
    statements.push_back(
        QString("INSERT INTO attendance(person_id, date, is_checked) "
                "VALUES((SELECT id FROM people WHERE display_name = %1), %2, 0)")
            .arg(sqlQuote(trimmed))
            .arg(sqlQuote(dayString(year, month, day))));
  }
  statements.push_back("COMMIT");

  if (!executePipeline(statements, &results, &error)) {
    lastError_ = error;
    QList<QString> rollback;
    rollback.push_back("ROLLBACK");
    executePipeline(rollback, nullptr, nullptr);
    return false;
  }

  return true;
}

bool JournalRemote::deleteUser(int year, int month, const QString& name) {
  lastError_.clear();
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QJsonArray results;
  QString error;
  const bool ok = executePipeline(
      {QString("DELETE FROM attendance "
               "WHERE person_id = (SELECT id FROM people WHERE display_name = %1) "
               "AND date LIKE '%2'")
           .arg(sqlQuote(trimmed))
           .arg(monthPattern(year, month))},
      &results, &error);
  if (!ok) {
    lastError_ = error;
  }
  return ok;
}

bool JournalRemote::getPersonProfile(const QString& name, PersonProfile* profile) {
  lastError_.clear();
  if (!profile) {
    return false;
  }

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QJsonArray results;
  QString error;
  if (!executePipeline(
          {QString("INSERT OR IGNORE INTO people(display_name) VALUES(%1)")
               .arg(sqlQuote(trimmed)),
           QString("SELECT display_name, age, profile_url, notes "
                   "FROM people WHERE display_name = %1 LIMIT 1")
               .arg(sqlQuote(trimmed))},
          &results, &error)) {
    lastError_ = error;
    return false;
  }

  if (results.size() < 2) {
    return false;
  }

  const QJsonArray rows = results.at(1).toObject().value("rows").toArray();
  if (rows.isEmpty()) {
    return false;
  }

  const QJsonArray row = rows.at(0).toArray();
  if (row.size() < 4) {
    return false;
  }

  profile->displayName = row.at(0).toObject().value("value").toString();
  profile->age = row.at(1).toObject().value("value").toString().toInt();
  profile->profileUrl = row.at(2).toObject().value("value").toString();
  profile->notes = row.at(3).toObject().value("value").toString();
  return true;
}

bool JournalRemote::updatePersonProfile(const QString& originalName,
                                        const PersonProfile& profile) {
  lastError_.clear();
  const QString oldName = originalName.trimmed();
  const QString newName = profile.displayName.trimmed();
  if (oldName.isEmpty() || newName.isEmpty()) {
    return false;
  }

  QList<QString> statements;
  statements.push_back("BEGIN");
  statements.push_back(
      QString("INSERT OR IGNORE INTO people(display_name) VALUES(%1)").arg(sqlQuote(oldName)));
  statements.push_back(
      QString("UPDATE people SET display_name = %1, age = %2, profile_url = %3, notes = %4 "
              "WHERE display_name = %5")
          .arg(sqlQuote(newName))
          .arg(profile.age > 0 ? QString::number(profile.age) : "NULL")
          .arg(sqlQuote(profile.profileUrl.trimmed()))
          .arg(sqlQuote(profile.notes.trimmed()))
          .arg(sqlQuote(oldName)));
  statements.push_back("COMMIT");

  QJsonArray results;
  QString error;
  if (!executePipeline(statements, &results, &error)) {
    lastError_ = error;
    executePipeline({"ROLLBACK"}, nullptr, nullptr);
    return false;
  }
  return true;
}

bool JournalRemote::ensureSchema(QString* errorMessage) {
  lastError_.clear();
  QJsonArray results;
  const QList<QString> schemaStatements = {
      "CREATE TABLE IF NOT EXISTS people ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "display_name TEXT NOT NULL UNIQUE, "
      "age INTEGER, "
      "profile_url TEXT, "
      "notes TEXT)",
      "CREATE TABLE IF NOT EXISTS person_photos ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "person_id INTEGER NOT NULL, "
      "file_path TEXT NOT NULL, "
      "sort_order INTEGER NOT NULL DEFAULT 0, "
      "FOREIGN KEY(person_id) REFERENCES people(id) ON DELETE CASCADE)",
      "CREATE TABLE IF NOT EXISTS attendance ("
      "person_id INTEGER NOT NULL, "
      "date TEXT NOT NULL, "
      "is_checked INTEGER NOT NULL, "
      "PRIMARY KEY(person_id, date), "
      "FOREIGN KEY(person_id) REFERENCES people(id) ON DELETE CASCADE)",
      "CREATE TABLE IF NOT EXISTS month_days ("
      "year INTEGER NOT NULL, "
      "month INTEGER NOT NULL, "
      "day INTEGER NOT NULL, "
      "PRIMARY KEY(year, month, day))"};

  const bool schemaOk = executePipeline(schemaStatements, &results, errorMessage);
  if (!schemaOk && errorMessage) {
    lastError_ = *errorMessage;
    return false;
  }

  QString error;
  if (!executePipeline(
          {"SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'users' LIMIT 1"},
          &results, &error)) {
    if (errorMessage) {
      *errorMessage = error;
    }
    lastError_ = error;
    return false;
  }

  if (results.isEmpty() || results.at(0).toObject().value("rows").toArray().isEmpty()) {
    return true;
  }

  bool hasLegacyArchive = false;
  if (!executePipeline({"SELECT name FROM sqlite_master WHERE type = 'table' "
                        "AND name = 'users_legacy' LIMIT 1"},
                       &results, &error)) {
    if (errorMessage) {
      *errorMessage = error;
    }
    lastError_ = error;
    return false;
  }
  hasLegacyArchive =
      !results.isEmpty() && !results.at(0).toObject().value("rows").toArray().isEmpty();

  QList<QString> migrationStatements;
  migrationStatements.push_back("BEGIN");
  migrationStatements.push_back(
      "INSERT OR IGNORE INTO people(display_name) "
      "SELECT DISTINCT name FROM users WHERE TRIM(name) <> ''");
  migrationStatements.push_back(
      "INSERT OR REPLACE INTO attendance(person_id, date, is_checked) "
      "SELECT people.id, users.date, users.is_checked "
      "FROM users JOIN people ON people.display_name = users.name");
  migrationStatements.push_back(hasLegacyArchive ? "DROP TABLE users"
                                                 : "ALTER TABLE users RENAME TO users_legacy");
  migrationStatements.push_back("COMMIT");

  if (!executePipeline(migrationStatements, &results, &error)) {
    executePipeline({"ROLLBACK"}, nullptr, nullptr);
    if (errorMessage) {
      *errorMessage = error;
    }
    lastError_ = error;
    return false;
  }

  return true;
}

QString JournalRemote::monthPattern(int year, int month) const {
  // Формат даты на remote: dd.MM.yyyy.
  // Для фильтра месяца в LIKE используем "__.MM.YYYY".
  return QString("__.%1.%2")
      .arg(month, 2, 10, QLatin1Char('0'))
      .arg(year, 4, 10, QLatin1Char('0'));
}

QString JournalRemote::dayString(int year, int month, int day) const {
  return QString("%1.%2.%3")
      .arg(day, 2, 10, QLatin1Char('0'))
      .arg(month, 2, 10, QLatin1Char('0'))
      .arg(year, 4, 10, QLatin1Char('0'));
}

int JournalRemote::daysInMonth(int year, int month) const {
  return QDate(year, month, 1).daysInMonth();
}

bool JournalRemote::executePipeline(const QList<QString>& sqlStatements,
                                    QJsonArray* outResults,
                                    QString* errorMessage) {
  // Преобразуем список SQL-команд в libsql JSON pipeline.
  QJsonArray requests;
  for (const QString& sql : sqlStatements) {
    QJsonObject stmt;
    stmt.insert("sql", sql);

    QJsonObject request;
    request.insert("type", "execute");
    request.insert("stmt", stmt);
    requests.push_back(request);
  }

  QJsonObject root;
  root.insert("requests", requests);

  QNetworkRequest request(QUrl(baseUrl_ + "/v2/pipeline"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  // Один HTTP POST может содержать несколько SQL-команд в нужном порядке.
  QNetworkReply* reply =
      network_.post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));

  // Синхронное ожидание ответа (через локальный event loop),
  // чтобы сохранить простой синхронный интерфейс IJournalStorage.
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

  timer.start(timeoutMs_);
  loop.exec();

  if (timer.isActive()) {
    timer.stop();
  } else {
    reply->abort();
    if (errorMessage) {
      *errorMessage = "Remote request timeout";
    }
    reply->deleteLater();
    return false;
  }

  if (reply->error() != QNetworkReply::NoError) {
    if (errorMessage) {
      *errorMessage = reply->errorString();
    }
    reply->deleteLater();
    return false;
  }

  const QByteArray body = reply->readAll();
  reply->deleteLater();

  // После этого блока считаем, что сеть сработала успешно;
  // дальше валидируем уже протокол и JSON-структуру ответа.
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (errorMessage) {
      *errorMessage = "Invalid JSON response from remote server";
    }
    return false;
  }

  const QJsonObject obj = doc.object();
  const QJsonArray results = obj.value("results").toArray();
  for (const QJsonValue& resultValue : results) {
    const QJsonObject result = resultValue.toObject();
    if (result.value("type").toString() != "ok") {
      if (errorMessage) {
        *errorMessage = result.value("error").toObject().value("message").toString(
            "Remote SQL execution failed");
      }
      return false;
    }
  }

  if (outResults) {
    // Возвращаем только "result" из каждого response, чтобы вызывающий код
    // работал с упрощенной структурой rows/cols.
    *outResults = QJsonArray();
    for (const QJsonValue& resultValue : results) {
      const QJsonObject response =
          resultValue.toObject().value("response").toObject();
      outResults->push_back(response.value("result").toObject());
    }
  }

  return true;
}

QString JournalRemote::sqlQuote(const QString& value) {
  // Минимальная защита для простого SQL-конструктора без prepared statements.
  QString escaped = value;
  escaped.replace("'", "''");
  return QString("'%1'").arg(escaped);
}
