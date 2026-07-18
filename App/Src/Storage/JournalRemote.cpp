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

constexpr int kRemoteProfileSchemaVersion = 3;
constexpr int kRemoteDayMarkerSchemaVersion = 4;
constexpr int kRemoteRankSchemaVersion = 5;
constexpr int kRemoteDevelopmentSchemaVersion = 6;
constexpr int kRemoteSchemaVersion = 7;

bool hasExactlyOneAffectedRow(const QJsonArray& results)
{
  if (results.size() != 1)
  {
    return false;
  }
  const QJsonValue value = results.at(0).toObject().value("affected_row_count");
  bool ok = false;
  const qint64 count = value.toVariant().toLongLong(&ok);
  return ok && count == 1;
}
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

MonthStateResult JournalRemote::getMonthState(int year, int month)
{
  lastError_.clear();
  if (!QDate(year, month, 1).isValid())
  {
    lastError_ = "Invalid year or month";
    return {MonthState::Error, lastError_};
  }

  QJsonArray results;
  QString error;
  const QString sql =
      QString("SELECT EXISTS(SELECT 1 FROM month_days WHERE year = %1 AND "
              "month = %2) OR EXISTS(SELECT 1 FROM month_participants WHERE "
              "year = %1 AND month = %2) OR EXISTS(SELECT 1 FROM attendance "
              "WHERE year = %1 AND month = %2)")
          .arg(year)
          .arg(month);
  if (!executePipeline({sql}, &results, &error) || results.size() != 1)
  {
    lastError_ =
        error.isEmpty() ? "Invalid remote month-state response" : error;
    return {MonthState::Error, lastError_};
  }
  const QJsonArray rows = results.at(0).toObject().value("rows").toArray();
  if (rows.size() != 1)
  {
    lastError_ = "Invalid remote month-state result";
    return {MonthState::Error, lastError_};
  }
  return {cellString(rows.at(0).toArray(), 0) == "1" ? MonthState::Ready
                                                     : MonthState::Missing,
          QString()};
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

std::vector<ParticipantDayMarker> JournalRemote::getDayMarkers(int year,
                                                               int month)
{
  lastError_.clear();
  std::vector<ParticipantDayMarker> result;
  if (!QDate(year, month, 1).isValid())
  {
    lastError_ = "Invalid year or month";
    return result;
  }
  QJsonArray results;
  QString error;
  const QString sql =
      QString("SELECT participant_id, day, kind_mask, note FROM "
              "participant_day_markers WHERE year = %1 AND month = %2 "
              "ORDER BY participant_id, day")
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
    bool kindsOk = false;
    const int day = cellString(row, 1).toInt(&dayOk);
    const int kindMask = cellString(row, 2).toInt(&kindsOk);
    const auto kinds = DayMarkerKindsFromInt(kindMask);
    ParticipantDayMarker marker{{cellString(row, 0)},
                                day,
                                kinds.value_or(DayMarkerKinds()),
                                cellString(row, 3)};
    if (row.size() < 4 || !dayOk || !kindsOk || !kinds.has_value() ||
        !marker.participantId.isValid() ||
        !QDate(year, month, marker.day).isValid() ||
        !IsValidDayMarkerKinds(marker.kinds) ||
        marker.note.size() > kMaxDayMarkerNoteLength)
    {
      result.clear();
      lastError_ = "Invalid remote participant day marker";
      return result;
    }
    result.push_back(std::move(marker));
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
      QString("SELECT p.id, p.display_name FROM "
              "month_participants mp JOIN "
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
      QString("SELECT participant_id, day, kind_mask, note FROM "
              "participant_day_markers WHERE year = %1 AND month = %2 "
              "ORDER BY participant_id, day")
          .arg(year)
          .arg(month),
      "COMMIT"};
  if (!executePipeline(sql, &results, &error) || results.size() != 4)
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
  for (const QJsonValue& value :
       results.at(3).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool dayOk = false;
    bool kindsOk = false;
    const int day = cellString(row, 1).toInt(&dayOk);
    const int kindMask = cellString(row, 2).toInt(&kindsOk);
    const auto kinds = DayMarkerKindsFromInt(kindMask);
    ParticipantDayMarker marker{{cellString(row, 0)},
                                day,
                                kinds.value_or(DayMarkerKinds()),
                                cellString(row, 3)};
    if (row.size() < 4 || !dayOk || !kindsOk || !kinds.has_value() ||
        !marker.participantId.isValid() ||
        !QDate(year, month, marker.day).isValid() ||
        !IsValidDayMarkerKinds(marker.kinds) ||
        marker.note.size() > kMaxDayMarkerNoteLength)
    {
      lastError_ = "Invalid remote participant day marker";
      return false;
    }
    loaded.dayMarkers.push_back(std::move(marker));
  }
  loaded.state = MonthState::Ready;
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

bool JournalRemote::saveDayMarker(int year, int month,
                                  const ParticipantDayMarker& marker)
{
  lastError_.clear();
  if (!QDate(year, month, marker.day).isValid() ||
      !marker.participantId.isValid() || !IsValidDayMarkerKinds(marker.kinds) ||
      marker.note.size() > kMaxDayMarkerNoteLength)
  {
    lastError_ = "Invalid participant day marker";
    return false;
  }
  const QString sql =
      QString("INSERT INTO participant_day_markers(year, month, day, "
              "participant_id, kind_mask, note) VALUES(%1, %2, %3, %4, %5, "
              "%6) ON CONFLICT(year, month, day, participant_id) DO UPDATE "
              "SET kind_mask = excluded.kind_mask, note = excluded.note")
          .arg(year)
          .arg(month)
          .arg(marker.day)
          .arg(sqlQuote(marker.participantId.value))
          .arg(marker.kinds.toInt())
          .arg(sqlQuote(marker.note));
  QString error;
  if (!executePipeline({"BEGIN", sql, "COMMIT"}, nullptr, &error))
  {
    lastError_ = error;
    return false;
  }
  return true;
}

bool JournalRemote::removeDayMarker(int year, int month,
                                    const ParticipantId& participantId, int day)
{
  lastError_.clear();
  if (!QDate(year, month, day).isValid() || !participantId.isValid())
  {
    lastError_ = "Invalid participant day marker key";
    return false;
  }
  const QString sql =
      QString("DELETE FROM participant_day_markers WHERE year = %1 AND "
              "month = %2 AND day = %3 AND participant_id = %4")
          .arg(year)
          .arg(month)
          .arg(day)
          .arg(sqlQuote(participantId.value));
  QString error;
  if (!executePipeline({sql}, nullptr, &error))
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
              "CONFLICT(id) DO NOTHING")
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
    sql.push_back(QString("INSERT OR IGNORE INTO month_days(year, month, day) "
                          "VALUES(%1, %2, %3)")
                      .arg(year)
                      .arg(month)
                      .arg(day));
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
      lastError_ = "Invalid or duplicate day marker snapshot";
      return false;
    }
    markerKeys.insert(key);
  }

  QList<QString> sql = {
      "PRAGMA foreign_keys = ON",
      "BEGIN",
      QString("DELETE FROM participant_day_markers WHERE year = %1 AND "
              "month = %2")
          .arg(year)
          .arg(month),
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
  for (const ParticipantDayMarker& marker : snapshot.dayMarkers)
  {
    sql.push_back(
        QString("INSERT INTO participant_day_markers(year, month, day, "
                "participant_id, kind_mask, note) VALUES(%1, %2, %3, %4, "
                "%5, %6)")
            .arg(year)
            .arg(month)
            .arg(marker.day)
            .arg(sqlQuote(marker.participantId.value))
            .arg(marker.kinds.toInt())
            .arg(sqlQuote(marker.note)));
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
  auto fail = [this, errorMessage](const QString& error)
  {
    lastError_ = error;
    if (errorMessage)
    {
      *errorMessage = error;
    }
    return false;
  };

  QJsonArray results;
  QString error;
  if (!executePipeline(
          {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
           "('users', 'journal_schema', 'participants')"},
          &results, &error))
  {
    return fail(error);
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
    return fail(
        "Legacy remote schema detected; automatic migration is refused");
  }

  const QString profileValidation =
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
  const QString insertTrigger =
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(profileValidation);
  const QString updateTrigger =
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(profileValidation);
  const QString rankedProfileValidation =
      profileValidation +
      " OR NEW.rank NOT IN ('page', 'squire', 'novice', 'recruit', "
      "'guest', 'knight')";
  const QString rankedInsertTrigger =
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(rankedProfileValidation);
  const QString rankedUpdateTrigger =
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes, rank "
              "ON participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(rankedProfileValidation);
  const QString detailedProfileValidation =
      rankedProfileValidation + " OR length(NEW.full_name) > 300 OR "
                                "instr(NEW.full_name, char(10)) != 0 OR "
                                "instr(NEW.full_name, char(13)) != 0 OR "
                                "length(NEW.contact) > 500 OR "
                                "instr(NEW.contact, char(10)) != 0 OR "
                                "instr(NEW.contact, char(13)) != 0";
  const QString detailedInsertTrigger =
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(detailedProfileValidation);
  const QString detailedUpdateTrigger =
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes, rank, "
              "full_name, contact ON participants WHEN %1 BEGIN "
              "SELECT RAISE(ABORT, 'invalid participant profile'); END")
          .arg(detailedProfileValidation);

  if (!tables.contains("journal_schema"))
  {
    if (tables.contains("participants"))
    {
      return fail("Partial unversioned remote schema detected");
    }
    const QList<QString> schema = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "CREATE TABLE journal_schema(version INTEGER NOT NULL)",
        QString("INSERT INTO journal_schema(version) VALUES(%1)")
            .arg(kRemoteSchemaVersion),
        "CREATE TABLE participants(id TEXT PRIMARY KEY NOT NULL, display_name "
        "TEXT NOT NULL CHECK(length(display_name) BETWEEN 1 AND 200), "
        "birth_day INTEGER CHECK(birth_day BETWEEN 1 AND 31), birth_month "
        "INTEGER CHECK(birth_month BETWEEN 1 AND 12), birth_year INTEGER "
        "CHECK(birth_year BETWEEN 1 AND 9999), notes TEXT NOT NULL DEFAULT '' "
        "CHECK(length(notes) <= 4096), rank TEXT NOT NULL DEFAULT 'guest' "
        "CHECK(rank IN ('page', 'squire', 'novice', 'recruit', 'guest', "
        "'knight')), full_name TEXT NOT NULL DEFAULT '' "
        "CHECK(length(full_name) <= 300 AND instr(full_name, char(10)) = 0 "
        "AND instr(full_name, char(13)) = 0), contact TEXT NOT NULL DEFAULT '' "
        "CHECK(length(contact) <= 500 AND instr(contact, char(10)) = 0 AND "
        "instr(contact, char(13)) = 0), created_at TEXT NOT NULL DEFAULT "
        "CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT "
        "CURRENT_TIMESTAMP, "
        "archived_at TEXT)",
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
        "CREATE TABLE participant_day_markers(year INTEGER NOT NULL "
        "CHECK(year BETWEEN 1 AND 9999), month INTEGER NOT NULL CHECK(month "
        "BETWEEN 1 AND 12), day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
        "participant_id TEXT NOT NULL, kind_mask INTEGER NOT NULL "
        "CHECK(typeof(kind_mask) = 'integer' AND kind_mask BETWEEN 1 AND 31), "
        "note TEXT NOT NULL DEFAULT '' "
        "CHECK(length(note) <= 500), PRIMARY KEY(year, month, day, "
        "participant_id), FOREIGN KEY(year, month, participant_id) "
        "REFERENCES month_participants(year, month, participant_id) ON "
        "DELETE CASCADE)",
        "CREATE INDEX idx_month_participants_order ON month_participants(year, "
        "month, sort_order, participant_id)",
        "CREATE INDEX idx_month_participants_history ON "
        "month_participants(participant_id, year, month)",
        "CREATE INDEX idx_attendance_history ON attendance(participant_id, "
        "year, month, day)",
        "CREATE INDEX idx_day_markers_history ON participant_day_markers("
        "participant_id, year, month, day)",
        detailedInsertTrigger,
        detailedUpdateTrigger,
        "COMMIT"};
    if (!executePipeline(schema, nullptr, &error))
    {
      return fail(error);
    }
  }

  results = {};
  if (!executePipeline({"SELECT version FROM journal_schema"}, &results,
                       &error) ||
      results.isEmpty())
  {
    return fail(error.isEmpty() ? "Cannot read remote schema version" : error);
  }
  const QJsonArray rows = results.at(0).toObject().value("rows").toArray();
  if (rows.size() != 1)
  {
    return fail("Remote schema version table must contain exactly one row");
  }
  bool versionOk = false;
  int version = cellString(rows.at(0).toArray(), 0).toInt(&versionOk);
  if (!versionOk)
  {
    return fail("Invalid remote schema version");
  }
  if (version == 2)
  {
    results = {};
    if (!executePipeline(
            {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
             "('journal_schema', 'participants', 'month_participants', "
             "'attendance', 'month_days')",
             "PRAGMA table_info(participants)",
             "PRAGMA table_info(month_participants)",
             "PRAGMA table_info(attendance)", "PRAGMA table_info(month_days)",
             "PRAGMA foreign_key_check",
             "SELECT id FROM participants WHERE length(trim(display_name)) = "
             "0 OR length(display_name) > 200 LIMIT 1"},
            &results, &error) ||
        results.size() != 7)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v2" : error);
    }
    QSet<QString> v2Tables;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      v2Tables.insert(cellString(value.toArray(), 0));
    }
    const QList<QPair<int, QSet<QString>>> v2Schema = {
        {1, {"id", "display_name", "created_at", "updated_at", "archived_at"}},
        {2, {"year", "month", "participant_id", "sort_order"}},
        {3, {"year", "month", "day", "participant_id", "is_checked"}},
        {4, {"year", "month", "day"}}};
    for (const auto& entry : v2Schema)
    {
      QSet<QString> columns;
      for (const QJsonValue& value :
           results.at(entry.first).toObject().value("rows").toArray())
      {
        columns.insert(cellString(value.toArray(), 1));
      }
      if (!columns.contains(entry.second))
      {
        return fail("Remote schema v2 columns are incomplete");
      }
    }
    if (v2Tables.size() != 5 ||
        !results.at(5).toObject().value("rows").toArray().isEmpty() ||
        !results.at(6).toObject().value("rows").toArray().isEmpty())
    {
      return fail("Remote schema v2 integrity check failed");
    }
    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "ALTER TABLE participants ADD COLUMN birth_day INTEGER "
        "CHECK(birth_day BETWEEN 1 AND 31)",
        "ALTER TABLE participants ADD COLUMN birth_month INTEGER "
        "CHECK(birth_month BETWEEN 1 AND 12)",
        "ALTER TABLE participants ADD COLUMN birth_year INTEGER "
        "CHECK(birth_year BETWEEN 1 AND 9999)",
        "ALTER TABLE participants ADD COLUMN notes TEXT NOT NULL DEFAULT '' "
        "CHECK(length(notes) <= 4096)",
        insertTrigger,
        updateTrigger,
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteProfileSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteProfileSchemaVersion;
  }
  if (version == kRemoteProfileSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
             "('journal_schema', 'participants', 'month_participants', "
             "'attendance', 'month_days')",
             "PRAGMA table_info(participants)",
             "PRAGMA table_info(month_participants)",
             "PRAGMA table_info(attendance)", "PRAGMA table_info(month_days)",
             "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')"},
            &results, &error) ||
        results.size() != 7)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v3" : error);
    }
    QSet<QString> v3Tables;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      v3Tables.insert(cellString(value.toArray(), 0));
    }
    const QList<QPair<int, QSet<QString>>> v3Schema = {
        {1,
         {"id", "display_name", "birth_day", "birth_month", "birth_year",
          "notes", "created_at", "updated_at", "archived_at"}},
        {2, {"year", "month", "participant_id", "sort_order"}},
        {3, {"year", "month", "day", "participant_id", "is_checked"}},
        {4, {"year", "month", "day"}}};
    for (const auto& entry : v3Schema)
    {
      QSet<QString> columns;
      for (const QJsonValue& value :
           results.at(entry.first).toObject().value("rows").toArray())
      {
        columns.insert(cellString(value.toArray(), 1));
      }
      if (!columns.contains(entry.second))
      {
        return fail("Remote schema v3 columns are incomplete");
      }
    }
    if (v3Tables.size() != 5 ||
        !results.at(5).toObject().value("rows").toArray().isEmpty() ||
        results.at(6).toObject().value("rows").toArray().size() != 2)
    {
      return fail("Remote schema v3 integrity check failed");
    }

    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "CREATE TABLE participant_day_markers(year INTEGER NOT NULL "
        "CHECK(year BETWEEN 1 AND 9999), month INTEGER NOT NULL CHECK(month "
        "BETWEEN 1 AND 12), day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
        "participant_id TEXT NOT NULL, kind_mask INTEGER NOT NULL "
        "CHECK(typeof(kind_mask) = 'integer' AND kind_mask BETWEEN 1 AND 15), "
        "note TEXT NOT NULL DEFAULT '' "
        "CHECK(length(note) <= 500), PRIMARY KEY(year, month, day, "
        "participant_id), FOREIGN KEY(year, month, participant_id) "
        "REFERENCES month_participants(year, month, participant_id) ON "
        "DELETE CASCADE)",
        "CREATE INDEX idx_day_markers_history ON participant_day_markers("
        "participant_id, year, month, day)",
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteDayMarkerSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteDayMarkerSchemaVersion;
  }
  if (version == kRemoteDayMarkerSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
             "('journal_schema', 'participants', 'month_participants', "
             "'attendance', 'month_days', 'participant_day_markers')",
             "PRAGMA table_info(participants)", "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')"},
            &results, &error) ||
        results.size() != 4)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v4" : error);
    }
    QSet<QString> participantColumns;
    for (const QJsonValue& value :
         results.at(1).toObject().value("rows").toArray())
    {
      participantColumns.insert(cellString(value.toArray(), 1));
    }
    const QSet<QString> requiredProfileColumns = {
        "id",    "display_name", "birth_day",  "birth_month", "birth_year",
        "notes", "created_at",   "updated_at", "archived_at"};
    if (!participantColumns.contains(requiredProfileColumns) ||
        !results.at(2).toObject().value("rows").toArray().isEmpty() ||
        results.at(3).toObject().value("rows").toArray().size() != 2)
    {
      return fail("Remote schema v4 integrity check failed");
    }

    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "ALTER TABLE participants ADD COLUMN rank TEXT NOT NULL DEFAULT "
        "'guest' CHECK(rank IN ('page', 'squire', 'novice', 'recruit', "
        "'guest', 'knight'))",
        "DROP TRIGGER participants_profile_insert",
        "DROP TRIGGER participants_profile_update",
        rankedInsertTrigger,
        rankedUpdateTrigger,
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteRankSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteRankSchemaVersion;
  }
  if (version == kRemoteRankSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
             "('journal_schema', 'participants', 'month_participants', "
             "'attendance', 'month_days', 'participant_day_markers')",
             "PRAGMA table_info(participants)",
             "PRAGMA table_info(month_participants)",
             "PRAGMA table_info(attendance)", "PRAGMA table_info(month_days)",
             "PRAGMA table_info(participant_day_markers)",
             "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')",
             "SELECT id FROM participants WHERE rank NOT IN ('page', "
             "'squire', 'novice', 'recruit', 'guest', 'knight') LIMIT 1"},
            &results, &error) ||
        results.size() != 9)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v5" : error);
    }
    QSet<QString> requiredTables;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      requiredTables.insert(cellString(value.toArray(), 0));
    }
    const QList<QPair<int, QSet<QString>>> v5Schema = {
        {1,
         {"id", "display_name", "birth_day", "birth_month", "birth_year",
          "notes", "rank", "created_at", "updated_at", "archived_at"}},
        {2, {"year", "month", "participant_id", "sort_order"}},
        {3, {"year", "month", "day", "participant_id", "is_checked"}},
        {4, {"year", "month", "day"}},
        {5, {"year", "month", "day", "participant_id", "kind_mask", "note"}}};
    for (const auto& entry : v5Schema)
    {
      QSet<QString> columns;
      for (const QJsonValue& value :
           results.at(entry.first).toObject().value("rows").toArray())
      {
        columns.insert(cellString(value.toArray(), 1));
      }
      if (!columns.contains(entry.second))
      {
        return fail("Remote schema v5 columns are incomplete");
      }
    }
    if (requiredTables.size() != 6 ||
        !results.at(6).toObject().value("rows").toArray().isEmpty() ||
        results.at(7).toObject().value("rows").toArray().size() != 2 ||
        !results.at(8).toObject().value("rows").toArray().isEmpty())
    {
      return fail("Remote schema v5 integrity check failed");
    }

    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "ALTER TABLE participants ADD COLUMN full_name TEXT NOT NULL DEFAULT "
        "'' CHECK(length(full_name) <= 300 AND "
        "instr(full_name, char(10)) = 0 AND "
        "instr(full_name, char(13)) = 0)",
        "ALTER TABLE participants ADD COLUMN contact TEXT NOT NULL DEFAULT '' "
        "CHECK(length(contact) <= 500 AND instr(contact, char(10)) = 0 AND "
        "instr(contact, char(13)) = 0)",
        "DROP INDEX IF EXISTS idx_day_markers_history",
        "ALTER TABLE participant_day_markers RENAME TO "
        "participant_day_markers_v5",
        "CREATE TABLE participant_day_markers(year INTEGER NOT NULL "
        "CHECK(year BETWEEN 1 AND 9999), month INTEGER NOT NULL CHECK(month "
        "BETWEEN 1 AND 12), day INTEGER NOT NULL CHECK(day BETWEEN 1 AND 31), "
        "participant_id TEXT NOT NULL, kind_mask INTEGER NOT NULL "
        "CHECK(typeof(kind_mask) = 'integer' AND kind_mask BETWEEN 1 AND 31), "
        "note TEXT NOT NULL DEFAULT '' CHECK(length(note) <= 500), PRIMARY "
        "KEY(year, month, day, participant_id), FOREIGN KEY(year, month, "
        "participant_id) REFERENCES month_participants(year, month, "
        "participant_id) ON DELETE CASCADE)",
        "INSERT INTO participant_day_markers(year, month, day, "
        "participant_id, kind_mask, note) SELECT year, month, day, "
        "participant_id, kind_mask, note FROM participant_day_markers_v5",
        "DROP TABLE participant_day_markers_v5",
        "CREATE INDEX idx_day_markers_history ON participant_day_markers("
        "participant_id, year, month, day)",
        "DROP TRIGGER participants_profile_insert",
        "DROP TRIGGER participants_profile_update",
        detailedInsertTrigger,
        detailedUpdateTrigger,
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteSchemaVersion;
  }
  if (version == kRemoteDevelopmentSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
             "('journal_schema', 'participants', 'month_participants', "
             "'attendance', 'month_days', 'participant_day_markers')",
             "PRAGMA table_info(participants)",
             "PRAGMA table_info(month_participants)",
             "PRAGMA table_info(attendance)", "PRAGMA table_info(month_days)",
             "PRAGMA table_info(participant_day_markers)",
             "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')",
             "SELECT id FROM participants WHERE rank NOT IN ('page', "
             "'squire', 'novice', 'recruit', 'guest', 'knight') OR "
             "length(full_name) > 300 OR "
             "instr(full_name, char(10)) != 0 OR "
             "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
             "instr(contact, char(10)) != 0 OR "
             "instr(contact, char(13)) != 0 LIMIT 1",
             "SELECT year FROM participant_day_markers WHERE "
             "typeof(kind_mask) != 'integer' OR kind_mask NOT BETWEEN 1 AND "
             "31 LIMIT 1",
             "SELECT sql FROM sqlite_master WHERE type = 'table' AND "
             "name = 'participant_day_markers'",
             "SELECT sql FROM sqlite_master WHERE type = 'table' AND "
             "name = 'participants'",
             "SELECT name FROM sqlite_master WHERE type = 'index' AND "
             "name = 'idx_day_markers_history'"},
            &results, &error) ||
        results.size() != 13)
    {
      return fail(error.isEmpty()
                      ? "Cannot verify development remote schema v6"
                      : error);
    }

    QSet<QString> requiredTables;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      requiredTables.insert(cellString(value.toArray(), 0));
    }
    QSet<QString> participantColumns;
    for (const QJsonValue& value :
         results.at(1).toObject().value("rows").toArray())
    {
      participantColumns.insert(cellString(value.toArray(), 1));
    }
    const bool hasTrainerColumn = participantColumns.remove("is_trainer");
    const QSet<QString> expectedParticipantColumns = {
        "id",          "display_name", "birth_day",  "birth_month",
        "birth_year",  "notes",        "rank",       "full_name",
        "contact",     "created_at",   "updated_at", "archived_at"};
    const QList<QPair<int, QSet<QString>>> expectedColumns = {
        {2, {"year", "month", "participant_id", "sort_order"}},
        {3, {"year", "month", "day", "participant_id", "is_checked"}},
        {4, {"year", "month", "day"}},
        {5,
         {"year", "month", "day", "participant_id", "kind_mask",
          "note"}}};
    bool columnsMatch = participantColumns == expectedParticipantColumns;
    for (const auto& entry : expectedColumns)
    {
      QSet<QString> actual;
      for (const QJsonValue& value :
           results.at(entry.first).toObject().value("rows").toArray())
      {
        actual.insert(cellString(value.toArray(), 1));
      }
      columnsMatch = columnsMatch && actual == entry.second;
    }
    const QJsonArray markerDefinitionRows =
        results.at(10).toObject().value("rows").toArray();
    const QString markerSql =
        markerDefinitionRows.isEmpty()
            ? QString()
            : cellString(markerDefinitionRows.at(0).toArray(), 0).simplified();
    const QJsonArray participantDefinitionRows =
        results.at(11).toObject().value("rows").toArray();
    const QString participantSql =
        participantDefinitionRows.isEmpty()
            ? QString()
            : cellString(participantDefinitionRows.at(0).toArray(), 0)
                  .simplified();
    const bool oldMarkerMask =
        markerSql.contains("kind_mask BETWEEN 1 AND 15",
                           Qt::CaseInsensitive);
    const bool currentMarkerMask =
        markerSql.contains("kind_mask BETWEEN 1 AND 31",
                           Qt::CaseInsensitive);
    if (requiredTables.size() != 6 || !columnsMatch ||
        !results.at(6).toObject().value("rows").toArray().isEmpty() ||
        results.at(7).toObject().value("rows").toArray().size() != 2 ||
        !results.at(8).toObject().value("rows").toArray().isEmpty() ||
        !results.at(9).toObject().value("rows").toArray().isEmpty() ||
        oldMarkerMask == currentMarkerMask ||
        !markerSql.contains("REFERENCES month_participants",
                            Qt::CaseInsensitive) ||
        !markerSql.contains("ON DELETE CASCADE", Qt::CaseInsensitive) ||
        !participantSql.contains(
            "full_name TEXT NOT NULL DEFAULT '' "
            "CHECK(length(full_name) <= 300 AND "
            "instr(full_name, char(10)) = 0 AND "
            "instr(full_name, char(13)) = 0)",
            Qt::CaseInsensitive) ||
        !participantSql.contains(
            "contact TEXT NOT NULL DEFAULT '' CHECK(length(contact) <= 500 "
            "AND instr(contact, char(10)) = 0 AND "
            "instr(contact, char(13)) = 0)",
            Qt::CaseInsensitive) ||
        (hasTrainerColumn &&
         (!participantSql.contains(
              "is_trainer INTEGER NOT NULL DEFAULT 0",
              Qt::CaseInsensitive) ||
          !participantSql.contains("is_trainer IN (0, 1)",
                                   Qt::CaseInsensitive))) ||
        results.at(12).toObject().value("rows").toArray().size() != 1)
    {
      return fail("Development remote schema v6 is not a known shape");
    }
    if (hasTrainerColumn)
    {
      results = {};
      if (!executePipeline(
              {"SELECT id FROM participants WHERE typeof(is_trainer) != "
               "'integer' OR is_trainer NOT IN (0, 1) LIMIT 1"},
              &results, &error) ||
          results.size() != 1 ||
          !results.at(0).toObject().value("rows").toArray().isEmpty())
      {
        return fail(error.isEmpty()
                        ? "Development remote trainer flags are invalid"
                        : error);
      }
    }

    QList<QString> migration = {"PRAGMA foreign_keys = ON", "BEGIN"};
    if (hasTrainerColumn)
    {
      migration.push_back(
          "CREATE TABLE IF NOT EXISTS "
          "legacy_v6_participant_trainer_flags(participant_id TEXT PRIMARY "
          "KEY NOT NULL, is_trainer INTEGER NOT NULL CHECK(is_trainer IN "
          "(0, 1)))");
      migration.push_back(
          "INSERT OR REPLACE INTO legacy_v6_participant_trainer_flags("
          "participant_id, is_trainer) SELECT id, is_trainer FROM "
          "participants");
    }
    migration.push_back("DROP TRIGGER participants_profile_insert");
    migration.push_back("DROP TRIGGER participants_profile_update");
    if (hasTrainerColumn)
    {
      migration.push_back(
          "ALTER TABLE participants DROP COLUMN is_trainer");
    }
    if (oldMarkerMask)
    {
      migration.push_back("DROP INDEX IF EXISTS idx_day_markers_history");
      migration.push_back(
          "ALTER TABLE participant_day_markers RENAME TO "
          "participant_day_markers_v6");
      migration.push_back(
          "CREATE TABLE participant_day_markers(year INTEGER NOT NULL "
          "CHECK(year BETWEEN 1 AND 9999), month INTEGER NOT NULL "
          "CHECK(month BETWEEN 1 AND 12), day INTEGER NOT NULL CHECK(day "
          "BETWEEN 1 AND 31), participant_id TEXT NOT NULL, kind_mask "
          "INTEGER NOT NULL CHECK(typeof(kind_mask) = 'integer' AND "
          "kind_mask BETWEEN 1 AND 31), note TEXT NOT NULL DEFAULT '' "
          "CHECK(length(note) <= 500), PRIMARY KEY(year, month, day, "
          "participant_id), FOREIGN KEY(year, month, participant_id) "
          "REFERENCES month_participants(year, month, participant_id) ON "
          "DELETE CASCADE)");
      migration.push_back(
          "INSERT INTO participant_day_markers(year, month, day, "
          "participant_id, kind_mask, note) SELECT year, month, day, "
          "participant_id, kind_mask, note FROM "
          "participant_day_markers_v6");
      migration.push_back("DROP TABLE participant_day_markers_v6");
      migration.push_back(
          "CREATE INDEX idx_day_markers_history ON "
          "participant_day_markers(participant_id, year, month, day)");
    }
    migration.push_back(detailedInsertTrigger);
    migration.push_back(detailedUpdateTrigger);
    migration.push_back(
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteSchemaVersion));
    migration.push_back("COMMIT");
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteSchemaVersion;
  }
  if (version != kRemoteSchemaVersion)
  {
    return fail(QString("Unsupported remote schema version: %1").arg(version));
  }

  results = {};
  if (!executePipeline(
          {"SELECT name FROM sqlite_master WHERE type = 'table' AND name IN "
           "('journal_schema', 'participants', 'month_participants', "
           "'attendance', 'month_days', 'participant_day_markers')",
           "PRAGMA table_info(participants)",
           "PRAGMA table_info(month_participants)",
           "PRAGMA table_info(attendance)", "PRAGMA table_info(month_days)",
           "PRAGMA table_info(participant_day_markers)",
           "PRAGMA foreign_key_check",
           "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
           "name IN ('participants_profile_insert', "
           "'participants_profile_update')",
           "SELECT id FROM participants WHERE rank NOT IN ('page', 'squire', "
           "'novice', 'recruit', 'guest', 'knight') OR "
           "length(full_name) > 300 OR "
           "instr(full_name, char(10)) != 0 OR "
           "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
           "instr(contact, char(10)) != 0 OR "
           "instr(contact, char(13)) != 0 LIMIT 1",
           "SELECT year FROM participant_day_markers WHERE "
           "typeof(kind_mask) != 'integer' OR kind_mask NOT BETWEEN 1 AND 31 "
           "LIMIT 1",
           "SELECT sql FROM sqlite_master WHERE type = 'table' AND "
           "name = 'participant_day_markers'",
           "SELECT sql FROM sqlite_master WHERE type = 'table' AND "
           "name = 'participants'",
           "SELECT name FROM sqlite_master WHERE type = 'index' AND "
           "name = 'idx_day_markers_history'"},
          &results, &error) ||
      results.size() != 13)
  {
    return fail(error.isEmpty() ? "Cannot verify remote schema" : error);
  }
  QSet<QString> requiredTables;
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    requiredTables.insert(cellString(value.toArray(), 0));
  }
  const QList<QPair<int, QSet<QString>>> schema = {
      {1,
       {"id", "display_name", "birth_day", "birth_month", "birth_year", "notes",
        "rank", "full_name", "contact", "created_at", "updated_at",
        "archived_at"}},
      {2, {"year", "month", "participant_id", "sort_order"}},
      {3, {"year", "month", "day", "participant_id", "is_checked"}},
      {4, {"year", "month", "day"}},
      {5, {"year", "month", "day", "participant_id", "kind_mask", "note"}}};
  for (const auto& entry : schema)
  {
    QSet<QString> columns;
    for (const QJsonValue& value :
         results.at(entry.first).toObject().value("rows").toArray())
    {
      columns.insert(cellString(value.toArray(), 1));
    }
    if (columns != entry.second)
    {
      return fail("Remote schema v7 columns are incomplete");
    }
    if (entry.first == 1 && columns.contains("is_trainer"))
    {
      return fail("Remote schema v7 contains obsolete trainer column");
    }
  }
  const QJsonArray markerDefinitionRows =
      results.at(10).toObject().value("rows").toArray();
  const QString markerSql =
      markerDefinitionRows.isEmpty()
          ? QString()
          : cellString(markerDefinitionRows.at(0).toArray(), 0).simplified();
  const QJsonArray participantDefinitionRows =
      results.at(11).toObject().value("rows").toArray();
  const QString participantSql =
      participantDefinitionRows.isEmpty()
          ? QString()
          : cellString(participantDefinitionRows.at(0).toArray(), 0)
                .simplified();
  if (requiredTables.size() != 6 ||
      !results.at(6).toObject().value("rows").toArray().isEmpty() ||
      results.at(7).toObject().value("rows").toArray().size() != 2 ||
      !results.at(8).toObject().value("rows").toArray().isEmpty() ||
      !results.at(9).toObject().value("rows").toArray().isEmpty() ||
      !markerSql.contains("kind_mask BETWEEN 1 AND 31",
                          Qt::CaseInsensitive) ||
      !markerSql.contains("REFERENCES month_participants",
                          Qt::CaseInsensitive) ||
      !markerSql.contains("ON DELETE CASCADE", Qt::CaseInsensitive) ||
      !participantSql.contains(
          "full_name TEXT NOT NULL DEFAULT '' CHECK(length(full_name) <= 300 "
          "AND instr(full_name, char(10)) = 0 AND "
          "instr(full_name, char(13)) = 0)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "contact TEXT NOT NULL DEFAULT '' CHECK(length(contact) <= 500 AND "
          "instr(contact, char(10)) = 0 AND "
          "instr(contact, char(13)) = 0)",
          Qt::CaseInsensitive) ||
      results.at(12).toObject().value("rows").toArray().size() != 1)
  {
    return fail("Remote schema integrity check failed");
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
std::optional<int> JournalRemote::cellOptionalInt(const QJsonArray& row,
                                                  int index)
{
  if (index < 0 || index >= row.size())
  {
    return std::nullopt;
  }
  const QJsonObject value = row.at(index).toObject();
  if (value.value("type").toString() == "null")
  {
    return std::nullopt;
  }
  bool ok = false;
  const int result = value.value("value").toString().toInt(&ok);
  return ok ? std::optional<int>(result) : std::nullopt;
}

std::optional<ParticipantProfile>
JournalRemote::profileFromRow(const QJsonArray& row)
{
  if (row.size() < 10)
  {
    return std::nullopt;
  }
  ParticipantProfile profile;
  profile.id = {cellString(row, 0)};
  profile.displayName = cellString(row, 1);
  const auto day = cellOptionalInt(row, 2);
  const auto month = cellOptionalInt(row, 3);
  const auto year = cellOptionalInt(row, 4);
  if (day.has_value() != month.has_value() ||
      (year.has_value() && !day.has_value()))
  {
    return std::nullopt;
  }
  if (day.has_value())
  {
    profile.birthday = Birthday{*day, *month, year};
  }
  const auto rank = ParticipantRankFromStorageValue(cellString(row, 5));
  if (!rank.has_value())
  {
    return std::nullopt;
  }
  profile.rank = *rank;
  profile.notes = cellString(row, 6);
  profile.archived = cellString(row, 7) == "1";
  profile.fullName = cellString(row, 8);
  profile.contact = cellString(row, 9);
  return profile.isValid() ? std::optional<ParticipantProfile>(profile)
                           : std::nullopt;
}

std::optional<ParticipantProfile>
JournalRemote::getParticipantProfile(const ParticipantId& id)
{
  lastError_.clear();
  if (!id.isValid())
  {
    lastError_ = "Invalid participant ID";
    return std::nullopt;
  }
  QJsonArray results;
  QString error;
  const QString sql =
      QString("SELECT id, display_name, birth_day, birth_month, birth_year, "
              "rank, notes, archived_at IS NOT NULL, full_name, contact "
              "FROM participants WHERE id = %1")
          .arg(sqlQuote(id.value));
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return std::nullopt;
  }
  const QJsonArray rows =
      results.isEmpty() ? QJsonArray()
                        : results.at(0).toObject().value("rows").toArray();
  if (rows.isEmpty())
  {
    return std::nullopt;
  }
  const auto profile = profileFromRow(rows.at(0).toArray());
  if (!profile.has_value())
  {
    lastError_ = "Remote participant profile is invalid";
  }
  return profile;
}

std::optional<std::vector<ParticipantProfile>>
JournalRemote::listParticipantProfiles(bool includeArchived)
{
  lastError_.clear();
  std::vector<ParticipantProfile> result;
  QJsonArray results;
  QString error;
  const QString sql =
      "SELECT id, display_name, birth_day, birth_month, birth_year, rank, "
      "notes, archived_at IS NOT NULL, full_name, contact FROM participants " +
      QString(includeArchived ? "" : "WHERE archived_at IS NULL ") +
      "ORDER BY CASE rank WHEN 'page' THEN 0 WHEN 'squire' THEN 1 WHEN "
      "'novice' THEN 2 WHEN 'recruit' THEN 3 WHEN 'guest' THEN 4 ELSE 5 END, "
      "lower(display_name), id";
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return std::nullopt;
  }
  if (results.isEmpty())
  {
    return result;
  }
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    const auto profile = profileFromRow(value.toArray());
    if (!profile.has_value())
    {
      lastError_ = "Remote participant profile is invalid";
      return std::nullopt;
    }
    result.push_back(*profile);
  }
  return result;
}

bool JournalRemote::updateParticipantProfile(const ParticipantProfile& profile)
{
  lastError_.clear();
  ParticipantProfile normalized = profile;
  normalized.displayName = normalized.displayName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  if (!normalized.isValid())
  {
    lastError_ = "Invalid participant profile";
    return false;
  }
  QString day = "NULL";
  QString month = "NULL";
  QString year = "NULL";
  if (normalized.birthday.has_value())
  {
    day = QString::number(normalized.birthday->day);
    month = QString::number(normalized.birthday->month);
    if (normalized.birthday->year.has_value())
    {
      year = QString::number(*normalized.birthday->year);
    }
  }
  const QString sql =
      QString("UPDATE participants SET display_name = %1, birth_day = %2, "
              "birth_month = %3, birth_year = %4, rank = %5, notes = %6, "
              "full_name = %7, contact = %8, "
              "updated_at = CURRENT_TIMESTAMP WHERE id = %9")
          .arg(sqlQuote(normalized.displayName))
          .arg(day)
          .arg(month)
          .arg(year)
          .arg(sqlQuote(ParticipantRankStorageValue(normalized.rank)))
          .arg(sqlQuote(normalized.notes))
          .arg(sqlQuote(normalized.fullName))
          .arg(sqlQuote(normalized.contact))
          .arg(sqlQuote(normalized.id.value));
  QJsonArray results;
  QString error;
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return false;
  }
  if (!hasExactlyOneAffectedRow(results))
  {
    lastError_ = "Remote participant update affected unexpected row count";
    return false;
  }
  return true;
}

bool JournalRemote::setParticipantArchived(const ParticipantId& id,
                                           bool archived)
{
  lastError_.clear();
  if (!id.isValid())
  {
    lastError_ = "Invalid participant ID";
    return false;
  }
  const QString sql =
      QString("UPDATE participants SET archived_at = CASE WHEN %1 = 1 THEN "
              "COALESCE(archived_at, CURRENT_TIMESTAMP) ELSE NULL END, "
              "updated_at = CURRENT_TIMESTAMP WHERE id = %2")
          .arg(archived ? 1 : 0)
          .arg(sqlQuote(id.value));
  QJsonArray results;
  QString error;
  if (!executePipeline({sql}, &results, &error))
  {
    lastError_ = error;
    return false;
  }
  if (!hasExactlyOneAffectedRow(results))
  {
    lastError_ = "Remote archive update affected unexpected row count";
    return false;
  }
  return true;
}
