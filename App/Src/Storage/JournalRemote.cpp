#include "JournalRemote.hpp"

#include <QDate>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{

constexpr int kRemoteSchemaVersion = 2;

} // namespace

JournalRemote::JournalRemote(const QString& baseUrl, int timeoutMs)
    : baseUrl_(baseUrl), timeoutMs_(timeoutMs)
{
}

bool JournalRemote::connect(QString* errorMessage)
{
  lastError_.clear();
  return ensureSchema(errorMessage);
}

QString JournalRemote::lastError() const
{
  return lastError_;
}

QString JournalRemote::cellString(const QJsonArray& row, int index)
{
  if (index < 0 || index >= row.size())
  {
    return {};
  }
  return row.at(index).toObject().value("value").toString();
}

std::vector<Participant> JournalRemote::getParticipantsForMonth(int year,
                                                                int month)
{
  lastError_.clear();
  std::vector<Participant> result;
  QJsonArray results;
  QString error;
  const QString sql =
      QString("SELECT p.id, p.display_name FROM month_participants mp "
              "JOIN participants p ON p.id = mp.participant_id "
              "WHERE mp.year = %1 AND mp.month = %2 "
              "ORDER BY mp.sort_order, p.id")
          .arg(year)
          .arg(month);
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return result;
  }
  if (results.isEmpty())
  {
    return result;
  }
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    if (row.size() >= 2)
    {
      result.push_back({{cellString(row, 0)}, cellString(row, 1)});
    }
  }
  return result;
}

QVector<int> JournalRemote::getActiveDays(int year, int month)
{
  lastError_.clear();
  QVector<int> days;
  QJsonArray results;
  QString error;
  const QString sql = QString("SELECT day FROM month_days WHERE year = %1 AND "
                              "month = %2 ORDER BY day")
                          .arg(year)
                          .arg(month);
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return days;
  }
  if (!results.isEmpty())
  {
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      bool ok = false;
      const int day = cellString(value.toArray(), 0).toInt(&ok);
      if (ok)
      {
        days.push_back(day);
      }
    }
  }
  return days.isEmpty() ? fullMonthDays(year, month) : days;
}

bool JournalRemote::saveActiveDays(int year, int month,
                                   const QVector<int>& days)
{
  lastError_.clear();
  QSet<int> unique;
  const int maxDay = daysInMonth(year, month);
  for (int day : days)
  {
    if (day < 1 || day > maxDay)
    {
      lastError_ = "Invalid active day";
      return false;
    }
    unique.insert(day);
  }
  if (unique.isEmpty())
  {
    lastError_ = "Active day set is empty";
    return false;
  }
  QVector<int> normalized(unique.begin(), unique.end());
  std::sort(normalized.begin(), normalized.end());

  QList<QString> sql = {
      "PRAGMA foreign_keys = ON", "BEGIN",
      QString("DELETE FROM month_days WHERE year = %1 AND month = %2")
          .arg(year)
          .arg(month)};
  for (int day : normalized)
  {
    sql.push_back(
        QString("INSERT INTO month_days(year, month, day) VALUES(%1, %2, %3)")
            .arg(year)
            .arg(month)
            .arg(day));
    sql.push_back(
        QString("INSERT OR IGNORE INTO attendance(year, month, day, "
                "participant_id, is_checked) SELECT %1, %2, %3, "
                "participant_id, 0 FROM month_participants WHERE year = "
                "%1 AND month = %2")
            .arg(year)
            .arg(month)
            .arg(day));
  }
  sql.push_back("COMMIT");
  QString error;
  if (!executePipeline(sql, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

std::vector<AttendanceRecord> JournalRemote::getMonth(int year, int month)
{
  lastError_.clear();
  std::vector<AttendanceRecord> result;
  QJsonArray results;
  QString error;
  const QString sql = QString("SELECT participant_id, day, is_checked FROM "
                              "attendance WHERE year = %1 "
                              "AND month = %2 ORDER BY participant_id, day")
                          .arg(year)
                          .arg(month);
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return result;
  }
  if (results.isEmpty())
  {
    return result;
  }
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool dayOk = false;
    const int day = cellString(row, 1).toInt(&dayOk);
    if (row.size() >= 3 && dayOk)
    {
      result.push_back({{cellString(row, 0)}, day, cellString(row, 2) == "1"});
    }
  }
  return result;
}

bool JournalRemote::getMonthSnapshot(int year, int month,
                                     MonthSnapshot* snapshot)
{
  lastError_.clear();
  if (!snapshot)
  {
    lastError_ = "Month snapshot output is null";
    return false;
  }
  QJsonArray results;
  QString error;
  const QList<QString> sql = {
      "BEGIN",
      QString("SELECT p.id, p.display_name FROM month_participants mp JOIN "
              "participants p ON p.id = mp.participant_id WHERE mp.year = %1 "
              "AND mp.month = %2 ORDER BY mp.sort_order, p.id")
          .arg(year)
          .arg(month),
      QString("SELECT day FROM month_days WHERE year = %1 AND month = %2 "
              "ORDER BY day")
          .arg(year)
          .arg(month),
      QString("SELECT participant_id, day, is_checked FROM attendance WHERE "
              "year = %1 AND month = %2 ORDER BY participant_id, day")
          .arg(year)
          .arg(month),
      "COMMIT"};
  if (!executePipeline(sql, &results, &error) || results.size() != 3)
  {
    lastError_ = error.isEmpty() ? "Invalid remote month snapshot" : error;
    return false;
  }

  MonthSnapshot loaded;
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    if (row.size() >= 2)
    {
      loaded.participants.push_back({{cellString(row, 0)}, cellString(row, 1)});
    }
  }
  for (const QJsonValue& value :
       results.at(1).toObject().value("rows").toArray())
  {
    bool ok = false;
    const int day = cellString(value.toArray(), 0).toInt(&ok);
    if (ok)
    {
      loaded.activeDays.push_back(day);
    }
  }
  if (loaded.activeDays.isEmpty())
  {
    loaded.activeDays = fullMonthDays(year, month);
  }
  for (const QJsonValue& value :
       results.at(2).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool ok = false;
    const int day = cellString(row, 1).toInt(&ok);
    if (row.size() >= 3 && ok)
    {
      loaded.attendance.push_back(
          {{cellString(row, 0)}, day, cellString(row, 2) == "1"});
    }
  }
  *snapshot = std::move(loaded);
  return true;
}
bool JournalRemote::saveAttendance(int year, int month,
                                   const std::vector<AttendanceRecord>& data)
{
  lastError_.clear();
  QList<QString> sql = {"PRAGMA foreign_keys = ON", "BEGIN"};
  for (const AttendanceRecord& record : data)
  {
    if (!record.participantId.isValid() ||
        !QDate(year, month, record.day).isValid())
    {
      lastError_ = "Invalid attendance record";
      return false;
    }
    sql.push_back(
        QString("INSERT INTO attendance(year, month, day, participant_id, "
                "is_checked) VALUES(%1, %2, %3, %4, %5) ON CONFLICT(year, "
                "month, day, participant_id) DO UPDATE SET is_checked = "
                "excluded.is_checked")
            .arg(year)
            .arg(month)
            .arg(record.day)
            .arg(sqlQuote(record.participantId.value))
            .arg(record.isChecked ? 1 : 0));
  }
  sql.push_back("COMMIT");
  QString error;
  if (!executePipeline(sql, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

bool JournalRemote::addParticipantToMonth(int year, int month,
                                          const Participant& participant)
{
  lastError_.clear();
  if (!participant.id.isValid() || participant.displayName.isEmpty())
  {
    lastError_ = "Invalid participant";
    return false;
  }
  const QVector<int> activeDays = getActiveDays(year, month);
  if (!lastError_.isEmpty())
  {
    return false;
  }
  QList<QString> sql = {
      "PRAGMA foreign_keys = ON", "BEGIN",
      QString("INSERT INTO participants(id, display_name) VALUES(%1, %2) ON "
              "CONFLICT(id) DO UPDATE SET display_name = "
              "excluded.display_name, updated_at = CURRENT_TIMESTAMP")
          .arg(sqlQuote(participant.id.value))
          .arg(sqlQuote(participant.displayName)),
      QString("INSERT OR IGNORE INTO month_participants(year, month, "
              "participant_id, sort_order) VALUES(%1, %2, %3, "
              "COALESCE((SELECT MAX(sort_order) + 1 FROM month_participants "
              "WHERE year = %1 AND month = %2), 0))")
          .arg(year)
          .arg(month)
          .arg(sqlQuote(participant.id.value))};
  for (int day : activeDays)
  {
    sql.push_back(
        QString("INSERT OR IGNORE INTO attendance(year, month, day, "
                "participant_id, is_checked) VALUES(%1, %2, %3, %4, 0)")
            .arg(year)
            .arg(month)
            .arg(day)
            .arg(sqlQuote(participant.id.value)));
  }
  sql.push_back("COMMIT");
  QString error;
  if (!executePipeline(sql, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

bool JournalRemote::removeParticipantFromMonth(int year, int month,
                                               const ParticipantId& id)
{
  lastError_.clear();
  if (!id.isValid())
  {
    return false;
  }
  const QList<QString> sql = {
      "PRAGMA foreign_keys = ON", "BEGIN",
      QString("DELETE FROM attendance WHERE year = %1 AND month = %2 AND "
              "participant_id = %3")
          .arg(year)
          .arg(month)
          .arg(sqlQuote(id.value)),
      QString("DELETE FROM month_participants WHERE year = %1 AND month = %2 "
              "AND participant_id = %3")
          .arg(year)
          .arg(month)
          .arg(sqlQuote(id.value)),
      "COMMIT"};
  QString error;
  if (!executePipeline(sql, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

bool JournalRemote::replaceMonth(int year, int month,
                                 const MonthSnapshot& snapshot)
{
  lastError_.clear();
  if (!QDate(year, month, 1).isValid() || snapshot.activeDays.isEmpty())
  {
    lastError_ = "Invalid month snapshot";
    return false;
  }
  QSet<QString> ids;
  for (const Participant& participant : snapshot.participants)
  {
    if (!participant.id.isValid() || participant.displayName.isEmpty() ||
        ids.contains(participant.id.value))
    {
      lastError_ = "Invalid participant snapshot";
      return false;
    }
    ids.insert(participant.id.value);
  }
  QSet<int> activeDays;
  for (int day : snapshot.activeDays)
  {
    if (!QDate(year, month, day).isValid() || activeDays.contains(day))
    {
      lastError_ = "Invalid or duplicate active day snapshot";
      return false;
    }
    activeDays.insert(day);
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
      lastError_ = "Invalid or duplicate attendance snapshot";
      return false;
    }
    attendanceKeys.insert(key);
  }

  QList<QString> sql = {
      "PRAGMA foreign_keys = ON", "BEGIN",
      QString("DELETE FROM attendance WHERE year = %1 AND month = %2")
          .arg(year)
          .arg(month),
      QString("DELETE FROM month_participants WHERE year = %1 AND month = %2")
          .arg(year)
          .arg(month),
      QString("DELETE FROM month_days WHERE year = %1 AND month = %2")
          .arg(year)
          .arg(month)};
  int sortOrder = 0;
  for (const Participant& participant : snapshot.participants)
  {
    sql.push_back(
        QString("INSERT INTO participants(id, display_name) VALUES(%1, %2) ON "
                "CONFLICT(id) DO NOTHING")
            .arg(sqlQuote(participant.id.value))
            .arg(sqlQuote(participant.displayName)));
    sql.push_back(
        QString("INSERT INTO month_participants(year, month, participant_id, "
                "sort_order) VALUES(%1, %2, %3, %4)")
            .arg(year)
            .arg(month)
            .arg(sqlQuote(participant.id.value))
            .arg(sortOrder++));
  }
  for (int day : snapshot.activeDays)
  {
    sql.push_back(
        QString("INSERT INTO month_days(year, month, day) VALUES(%1, %2, %3)")
            .arg(year)
            .arg(month)
            .arg(day));
  }
  for (const AttendanceRecord& record : snapshot.attendance)
  {
    sql.push_back(
        QString("INSERT INTO attendance(year, month, day, participant_id, "
                "is_checked) VALUES(%1, %2, %3, %4, %5)")
            .arg(year)
            .arg(month)
            .arg(record.day)
            .arg(sqlQuote(record.participantId.value))
            .arg(record.isChecked ? 1 : 0));
  }
  sql.push_back("COMMIT");
  QString error;
  if (!executePipeline(sql, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

bool JournalRemote::ensureSchema(QString* errorMessage)
{
  QJsonArray results;
  QString error;
  if (!executePipeline(
          {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
           "('users', 'journal_schema', 'participants')"},
          &results, &error))
  {
    lastError_ = error;
    if (errorMessage)
    {
      *errorMessage = error;
    }
    return false;
  }
  QSet<QString> tables;
  if (!results.isEmpty())
  {
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      tables.insert(cellString(value.toArray(), 0));
    }
  }
  if (tables.contains("users") && !tables.contains("journal_schema"))
  {
    lastError_ =
        "Legacy remote schema detected; automatic migration is refused";
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  if (!tables.contains("journal_schema"))
  {
    if (tables.contains("participants"))
    {
      lastError_ = "Partial unversioned remote schema detected";
      if (errorMessage)
      {
        *errorMessage = lastError_;
      }
      return false;
    }
    const QList<QString> schema = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "CREATE TABLE journal_schema(version INTEGER NOT NULL)",
        QString("INSERT INTO journal_schema(version) VALUES(%1)")
            .arg(kRemoteSchemaVersion),
        "CREATE TABLE participants(id TEXT PRIMARY KEY NOT NULL, display_name "
        "TEXT NOT NULL CHECK(length(display_name) > 0), created_at TEXT NOT "
        "NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT "
        "CURRENT_TIMESTAMP, archived_at TEXT)",
        "CREATE TABLE month_participants(year INTEGER NOT NULL CHECK(year "
        "BETWEEN 1 AND 9999), month INTEGER NOT NULL CHECK(month BETWEEN 1 AND "
        "12), participant_id TEXT NOT NULL, sort_order INTEGER NOT NULL "
        "CHECK(sort_order >= 0), PRIMARY KEY(year, month, participant_id), "
        "FOREIGN KEY(participant_id) REFERENCES participants(id))",
        "CREATE TABLE attendance(year INTEGER NOT NULL CHECK(year BETWEEN 1 "
        "AND 9999), month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), day "
        "INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), participant_id TEXT NOT "
        "NULL, is_checked INTEGER NOT NULL CHECK(is_checked IN (0, 1)), "
        "PRIMARY KEY(year, month, day, participant_id), FOREIGN KEY(year, "
        "month, participant_id) REFERENCES month_participants(year, month, "
        "participant_id) ON DELETE CASCADE)",
        "CREATE TABLE month_days(year INTEGER NOT NULL CHECK(year BETWEEN 1 "
        "AND 9999), month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12), day "
        "INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), PRIMARY KEY(year, "
        "month, day))",
        "CREATE INDEX idx_month_participants_order ON month_participants(year, "
        "month, sort_order, participant_id)",
        "CREATE INDEX idx_month_participants_history ON "
        "month_participants(participant_id, year, month)",
        "CREATE INDEX idx_attendance_history ON attendance(participant_id, "
        "year, month, day)",
        "COMMIT"};
    if (!executePipeline(schema, nullptr, &error))
    {
      lastError_ = error;
      if (errorMessage)
      {
        *errorMessage = error;
      }
      return false;
    }
    return true;
  }

  results = {};
  if (!executePipeline({"SELECT version FROM journal_schema"}, &results,
                       &error) ||
      results.isEmpty())
  {
    lastError_ = error.isEmpty() ? "Cannot read remote schema version" : error;
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  const QJsonArray rows = results.at(0).toObject().value("rows").toArray();
  bool versionOk = false;
  const int version =
      rows.isEmpty() ? 0
                     : cellString(rows.at(0).toArray(), 0).toInt(&versionOk);
  if (!versionOk || version != kRemoteSchemaVersion)
  {
    lastError_ = QString("Unsupported remote schema version: %1").arg(version);
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  results = {};
  if (!executePipeline(
          {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
           "('journal_schema', 'participants', 'month_participants', "
           "'attendance', 'month_days')",
           "PRAGMA foreign_key_check"},
          &results, &error) ||
      results.size() != 2)
  {
    lastError_ = error.isEmpty() ? "Cannot verify remote schema" : error;
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  QSet<QString> requiredTables;
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    requiredTables.insert(cellString(value.toArray(), 0));
  }
  if (requiredTables.size() != 5 ||
      !results.at(1).toObject().value("rows").toArray().isEmpty())
  {
    lastError_ = "Remote schema integrity check failed";
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  return true;
}

int JournalRemote::daysInMonth(int year, int month) const
{
  return QDate(year, month, 1).daysInMonth();
}

QVector<int> JournalRemote::fullMonthDays(int year, int month) const
{
  QVector<int> result;
  const int count = daysInMonth(year, month);
  for (int day = 1; day <= count; ++day)
  {
    result.push_back(day);
  }
  return result;
}

bool JournalRemote::executePipeline(const QList<QString>& sqlStatements,
                                    QJsonArray* outResults,
                                    QString* errorMessage)
{
  const bool atomic = sqlStatements.contains("BEGIN");
  QJsonArray requests;

  auto statementObject = [](const QString& sql)
  {
    QJsonObject statement;
    statement.insert("sql", sql);
    return statement;
  };
  auto okCondition = [](int step)
  {
    QJsonObject condition;
    condition.insert("type", "ok");
    condition.insert("step", step);
    return condition;
  };

  int expectedBatchSteps = 0;
  int batchMutationCount = 0;
  if (atomic)
  {
    QList<QString> mutations;
    for (const QString& sql : sqlStatements)
    {
      if (sql != "BEGIN" && sql != "COMMIT" && sql != "ROLLBACK" &&
          sql != "PRAGMA foreign_keys = ON")
      {
        mutations.push_back(sql);
      }
    }

    QJsonArray steps;
    QJsonObject pragmaStep;
    pragmaStep.insert("stmt", statementObject("PRAGMA foreign_keys = ON"));
    steps.push_back(pragmaStep);

    QJsonObject beginStep;
    beginStep.insert("condition", okCondition(0));
    beginStep.insert("stmt", statementObject("BEGIN"));
    steps.push_back(beginStep);
    int previousStep = 1;

    for (const QString& sql : mutations)
    {
      QJsonObject step;
      step.insert("condition", okCondition(previousStep));
      step.insert("stmt", statementObject(sql));
      steps.push_back(step);
      previousStep = steps.size() - 1;
    }

    const int commitStepIndex = steps.size();
    QJsonObject commitStep;
    commitStep.insert("condition", okCondition(previousStep));
    commitStep.insert("stmt", statementObject("COMMIT"));
    steps.push_back(commitStep);

    QJsonObject rollbackCondition;
    rollbackCondition.insert("type", "not");
    rollbackCondition.insert("cond", okCondition(commitStepIndex));
    QJsonObject rollbackStep;
    rollbackStep.insert("condition", rollbackCondition);
    rollbackStep.insert("stmt", statementObject("ROLLBACK"));
    steps.push_back(rollbackStep);

    QJsonObject batch;
    batch.insert("steps", steps);
    QJsonObject request;
    request.insert("type", "batch");
    request.insert("batch", batch);
    requests.push_back(request);
    expectedBatchSteps = steps.size();
    batchMutationCount = mutations.size();
  }
  else
  {
    for (const QString& sql : sqlStatements)
    {
      QJsonObject request;
      request.insert("type", "execute");
      request.insert("stmt", statementObject(sql));
      requests.push_back(request);
    }
  }

  QJsonObject root;
  root.insert("requests", requests);
  QNetworkRequest request(QUrl(baseUrl_ + "/v2/pipeline"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QNetworkReply* reply = network_.post(
      request, QJsonDocument(root).toJson(QJsonDocument::Compact));
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeoutMs_);
  loop.exec();
  if (!timer.isActive())
  {
    reply->abort();
    if (errorMessage)
    {
      *errorMessage = "Remote request timeout";
    }
    reply->deleteLater();
    return false;
  }
  timer.stop();
  if (reply->error() != QNetworkReply::NoError)
  {
    if (errorMessage)
    {
      *errorMessage = reply->errorString();
    }
    reply->deleteLater();
    return false;
  }
  const QByteArray body = reply->readAll();
  reply->deleteLater();
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    if (errorMessage)
    {
      *errorMessage = "Invalid JSON response from remote server";
    }
    return false;
  }
  const QJsonArray results = document.object().value("results").toArray();
  for (const QJsonValue& value : results)
  {
    const QJsonObject result = value.toObject();
    if (result.value("type").toString() != "ok")
    {
      if (errorMessage)
      {
        *errorMessage =
            result.value("error").toObject().value("message").toString(
                "Remote SQL execution failed");
      }
      return false;
    }
  }

  if (atomic)
  {
    if (results.size() != 1)
    {
      if (errorMessage)
      {
        *errorMessage = "Invalid remote batch response";
      }
      return false;
    }
    const QJsonObject response =
        results.at(0).toObject().value("response").toObject();
    const QJsonObject batchResult = response.value("result").toObject();
    const QJsonArray stepErrors = batchResult.value("step_errors").toArray();
    if (stepErrors.size() != expectedBatchSteps)
    {
      if (errorMessage)
      {
        *errorMessage = "Invalid remote batch step count";
      }
      return false;
    }
    for (const QJsonValue& stepError : stepErrors)
    {
      if (!stepError.isNull() && !stepError.isUndefined())
      {
        if (errorMessage)
        {
          *errorMessage = stepError.toObject().value("message").toString(
              "Remote atomic batch failed");
        }
        return false;
      }
    }
    if (outResults)
    {
      const QJsonArray stepResults =
          batchResult.value("step_results").toArray();
      if (stepResults.size() != expectedBatchSteps)
      {
        if (errorMessage)
        {
          *errorMessage = "Invalid remote batch result count";
        }
        return false;
      }
      *outResults = {};
      for (int index = 0; index < batchMutationCount; ++index)
      {
        outResults->push_back(stepResults.at(index + 2).toObject());
      }
    }
    return true;
  }

  if (outResults)
  {
    *outResults = {};
    for (const QJsonValue& value : results)
    {
      outResults->push_back(value.toObject()
                                .value("response")
                                .toObject()
                                .value("result")
                                .toObject());
    }
  }
  return true;
}

QString JournalRemote::sqlQuote(const QString& value)
{
  QString escaped = value;
  escaped.replace("'", "''");
  return QString("'%1'").arg(escaped);
}
