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

  // Remote storage хранит одну строку на (user, day), поэтому users берем через DISTINCT.
  const QString sql =
      QString("SELECT DISTINCT name FROM users WHERE date LIKE '%1' ORDER BY name ASC")
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

  // Возвращаем плоский список записей; группировка по пользователям делается уже в UI.
  const QString sql = QString(
      "SELECT name, date, is_checked FROM users WHERE date LIKE '%1' ORDER BY "
      "name ASC, date ASC")
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
               "SELECT 1 FROM users WHERE name = %1 AND date LIKE '%2' LIMIT 1")
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
  const int maxDay = daysInMonth(year, month);
  // Как и в local storage, создаем набор строк сразу на все дни месяца.
  for (int day = 1; day <= maxDay; ++day) {
    statements.push_back(
        QString("INSERT INTO users(name, date, is_checked) VALUES(%1, %2, 0)")
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
      {QString("DELETE FROM users WHERE name = %1 AND date LIKE '%2'")
           .arg(sqlQuote(trimmed))
           .arg(monthPattern(year, month))},
      &results, &error);
  if (!ok) {
    lastError_ = error;
  }
  return ok;
}

bool JournalRemote::ensureSchema(QString* errorMessage) {
  lastError_.clear();
  QJsonArray results;
  // Схема создается лениво, чтобы пустой сервер поднимался без ручной миграции.
  const bool ok = executePipeline(
      {"CREATE TABLE IF NOT EXISTS users ("
       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
       "name TEXT NOT NULL, "
       "date TEXT NOT NULL, "
       "is_checked INTEGER NOT NULL)"},
      &results, errorMessage);
  if (!ok && errorMessage) {
    lastError_ = *errorMessage;
  }
  return ok;
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
