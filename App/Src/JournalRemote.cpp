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
  return ensureSchema(errorMessage);
}

QStringList JournalRemote::getUsersForMonth(int year, int month) {
  QStringList users;
  QJsonArray results;

  const QString sql =
      QString("SELECT DISTINCT name FROM users WHERE date LIKE '%1' ORDER BY name ASC")
          .arg(monthPattern(year, month));

  QString error;
  if (!executePipeline({sql}, &results, &error)) {
    return users;
  }

  if (results.isEmpty()) {
    return users;
  }

  const QJsonObject result = results.at(0).toObject();
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

std::vector<AttendanceRecord> JournalRemote::getMonth(int year, int month) {
  std::vector<AttendanceRecord> records;
  QJsonArray results;

  const QString sql = QString(
      "SELECT name, date, is_checked FROM users WHERE date LIKE '%1' ORDER BY "
      "name ASC, date ASC")
                          .arg(monthPattern(year, month));

  QString error;
  if (!executePipeline({sql}, &results, &error)) {
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
    const bool isChecked = row.at(2).toObject().value("value").toString() == "1";

    records.push_back({userName, day, isChecked});
  }

  return records;
}

bool JournalRemote::saveMonth(int year, int month,
                              const std::vector<AttendanceRecord>& data) {
  QList<QString> statements;
  statements.push_back("BEGIN");
  statements.push_back(
      QString("DELETE FROM users WHERE date LIKE '%1'").arg(monthPattern(year, month)));

  for (const AttendanceRecord& record : data) {
    statements.push_back(
        QString("INSERT INTO users(name, date, is_checked) VALUES(%1, %2, %3)")
            .arg(sqlQuote(record.userName))
            .arg(sqlQuote(dayString(year, month, record.day)))
            .arg(record.isChecked ? "1" : "0"));
  }
  statements.push_back("COMMIT");

  QJsonArray results;
  QString error;
  if (!executePipeline(statements, &results, &error)) {
    QList<QString> rollback;
    rollback.push_back("ROLLBACK");
    executePipeline(rollback, nullptr, nullptr);
    return false;
  }

  return true;
}

bool JournalRemote::addUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QJsonArray results;
  QString error;
  if (!executePipeline(
          {QString(
               "SELECT 1 FROM users WHERE name = %1 AND date LIKE '%2' LIMIT 1")
               .arg(sqlQuote(trimmed))
               .arg(monthPattern(year, month))},
          &results, &error)) {
    return false;
  }

  if (!results.isEmpty() &&
      !results.at(0).toObject().value("rows").toArray().isEmpty()) {
    return false;
  }

  QList<QString> statements;
  statements.push_back("BEGIN");
  const int maxDay = daysInMonth(year, month);
  for (int day = 1; day <= maxDay; ++day) {
    statements.push_back(
        QString("INSERT INTO users(name, date, is_checked) VALUES(%1, %2, 0)")
            .arg(sqlQuote(trimmed))
            .arg(sqlQuote(dayString(year, month, day))));
  }
  statements.push_back("COMMIT");

  if (!executePipeline(statements, &results, &error)) {
    QList<QString> rollback;
    rollback.push_back("ROLLBACK");
    executePipeline(rollback, nullptr, nullptr);
    return false;
  }

  return true;
}

bool JournalRemote::deleteUser(int year, int month, const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  QJsonArray results;
  QString error;
  return executePipeline(
      {QString("DELETE FROM users WHERE name = %1 AND date LIKE '%2'")
           .arg(sqlQuote(trimmed))
           .arg(monthPattern(year, month))},
      &results, &error);
}

bool JournalRemote::ensureSchema(QString* errorMessage) {
  QJsonArray results;
  return executePipeline(
      {"CREATE TABLE IF NOT EXISTS users ("
       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
       "name TEXT NOT NULL, "
       "date TEXT NOT NULL, "
       "is_checked INTEGER NOT NULL)"},
      &results, errorMessage);
}

QString JournalRemote::monthPattern(int year, int month) const {
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

  QNetworkReply* reply =
      network_.post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));

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
  QString escaped = value;
  escaped.replace("'", "''");
  return QString("'%1'").arg(escaped);
}
