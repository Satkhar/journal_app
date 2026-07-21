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
constexpr int kRemoteParticipantDetailsSchemaVersion = 7;
constexpr int kRemoteParticipantNameSchemaVersion = 8;
constexpr int kRemoteSchemaVersion = 9;

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
    : JournalRemote(RemoteConnectionOptions{baseUrl, QString(), timeoutMs,
                                            false})
{
}

JournalRemote::JournalRemote(RemoteConnectionOptions options)
    : options_(std::move(options))
{
}

bool JournalRemote::connect(QString* errorMessage)
{
  lastError_.clear();
  if (errorMessage)
  {
    errorMessage->clear();
  }
  const auto normalized =
      NormalizeRemoteConnectionOptions(options_, &lastError_);
  if (!normalized.has_value())
  {
    if (errorMessage)
    {
      *errorMessage = lastError_;
    }
    return false;
  }
  options_ = *normalized;
  return ensureSchema(errorMessage);
}

QString JournalRemote::lastError() const
{
  return lastError_;
}

std::optional<std::vector<JournalMonth>> JournalRemote::listMonths()
{
  lastError_.clear();
  QJsonArray results;
  QString error;
  const QString sql =
      "SELECT year, month FROM ("
      "SELECT year, month FROM month_days UNION "
      "SELECT year, month FROM month_participants UNION "
      "SELECT year, month FROM attendance) "
      "ORDER BY year DESC, month DESC";
  if (!executePipeline({sql}, &results, &error) || results.size() != 1)
  {
    lastError_ = error.isEmpty() ? "Invalid remote month list" : error;
    return std::nullopt;
  }

  std::vector<JournalMonth> result;
  for (const QJsonValue& value :
       results.at(0).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool yearOk = false;
    bool monthOk = false;
    const int year = cellString(row, 0).toInt(&yearOk);
    const int month = cellString(row, 1).toInt(&monthOk);
    if (row.size() < 2 || !yearOk || !monthOk ||
        !QDate(year, month, 1).isValid())
    {
      lastError_ = "Invalid configured month in remote database";
      return std::nullopt;
    }
    result.push_back({year, month});
  }
  return result;
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
      QString("SELECT p.id, p.display_name, p.historical_name, p.full_name "
              "FROM month_participants mp "
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
    if (row.size() >= 4)
    {
      result.push_back({{cellString(row, 0)}, cellString(row, 1),
                        cellString(row, 2), cellString(row, 3)});
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

MonthSnapshot JournalRemote::loadMonthSnapshot(int year, int month)
{
  lastError_.clear();
  MonthSnapshot loaded;
  auto fail = [this](const QString& error)
  {
    lastError_ = error;
    MonthSnapshot failed;
    failed.errorMessage = lastError_;
    return failed;
  };
  if (!QDate(year, month, 1).isValid())
  {
    return fail("Invalid year or month");
  }

  QJsonArray results;
  QString error;
  const QList<QString> sql = {
      "BEGIN",
      QString("SELECT EXISTS(SELECT 1 FROM month_days WHERE year = %1 AND "
              "month = %2) OR EXISTS(SELECT 1 FROM month_participants WHERE "
              "year = %1 AND month = %2) OR EXISTS(SELECT 1 FROM attendance "
              "WHERE year = %1 AND month = %2)")
          .arg(year)
          .arg(month),
      QString("SELECT p.id, p.display_name, p.historical_name, p.full_name "
              "FROM "
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
  if (!executePipeline(sql, &results, &error) || results.size() != 5)
  {
    return fail(error.isEmpty() ? "Invalid remote month snapshot" : error);
  }

  const QJsonArray stateRows =
      results.at(0).toObject().value("rows").toArray();
  if (stateRows.size() != 1)
  {
    return fail("Invalid remote month-state result");
  }
  const QString stateValue = cellString(stateRows.at(0).toArray(), 0);
  if (stateValue != "0" && stateValue != "1")
  {
    return fail("Invalid remote month-state value");
  }
  loaded.state =
      stateValue == "1" ? MonthState::Ready : MonthState::Missing;
  QSet<QString> participantIds;
  for (const QJsonValue& value :
       results.at(1).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    if (row.size() < 4)
    {
      return fail("Invalid remote participant snapshot");
    }
    Participant participant{{cellString(row, 0)}, cellString(row, 1),
                            cellString(row, 2), cellString(row, 3)};
    if (!IsValidParticipantSnapshot(participant) ||
        participantIds.contains(participant.id.value))
    {
      return fail("Invalid remote participant snapshot");
    }
    participantIds.insert(participant.id.value);
    loaded.participants.push_back(std::move(participant));
  }
  QSet<int> activeDaySet;
  for (const QJsonValue& value :
       results.at(2).toObject().value("rows").toArray())
  {
    bool ok = false;
    const int day = cellString(value.toArray(), 0).toInt(&ok);
    if (!ok || !QDate(year, month, day).isValid() ||
        activeDaySet.contains(day))
    {
      return fail("Invalid remote active day");
    }
    activeDaySet.insert(day);
    loaded.activeDays.push_back(day);
  }
  if (loaded.activeDays.isEmpty())
  {
    loaded.activeDays = fullMonthDays(year, month);
  }
  QSet<QString> attendanceKeys;
  for (const QJsonValue& value :
       results.at(3).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool ok = false;
    const int day = cellString(row, 1).toInt(&ok);
    const ParticipantId participantId{cellString(row, 0)};
    const QString checked = cellString(row, 2);
    const QString key =
        participantId.value + ':' + QString::number(day);
    if (row.size() < 3 || !ok || !participantId.isValid() ||
        !participantIds.contains(participantId.value) ||
        !QDate(year, month, day).isValid() ||
        (checked != "0" && checked != "1") ||
        attendanceKeys.contains(key))
    {
      return fail("Invalid remote attendance snapshot");
    }
    attendanceKeys.insert(key);
    loaded.attendance.push_back({participantId, day, checked == "1"});
  }
  QSet<QString> markerKeys;
  for (const QJsonValue& value :
       results.at(4).toObject().value("rows").toArray())
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
    const QString key = marker.participantId.value + ':' +
                        QString::number(marker.day);
    if (row.size() < 4 || !dayOk || !kindsOk || !kinds.has_value() ||
        !marker.participantId.isValid() ||
        !participantIds.contains(marker.participantId.value) ||
        !QDate(year, month, marker.day).isValid() ||
        !IsValidDayMarkerKinds(marker.kinds) ||
        marker.note.size() > kMaxDayMarkerNoteLength ||
        markerKeys.contains(key))
    {
      return fail("Invalid remote participant day marker");
    }
    markerKeys.insert(key);
    loaded.dayMarkers.push_back(std::move(marker));
  }
  return loaded;
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
                                          const ParticipantProfile& profile)
{
  lastError_.clear();
  ParticipantProfile normalized = profile;
  normalized.historicalName = normalized.historicalName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  normalized.displayName = ParticipantDisplayName(normalized);
  if (!QDate(year, month, 1).isValid() ||
      (normalized.historicalName.isEmpty() && normalized.fullName.isEmpty()) ||
      !normalized.isValid() ||
      !IsTrainingStartMonthNotAfter(normalized.trainingStartMonth,
                                    QDate::currentDate()) ||
      normalized.archived)
  {
    lastError_ = "Invalid participant";
    return false;
  }
  const QVector<int> activeDays = getActiveDays(year, month);
  if (!lastError_.isEmpty())
  {
    return false;
  }
  QString birthDay = "NULL";
  QString birthMonth = "NULL";
  QString birthYear = "NULL";
  if (normalized.birthday.has_value())
  {
    birthDay = QString::number(normalized.birthday->day);
    birthMonth = QString::number(normalized.birthday->month);
    if (normalized.birthday->year.has_value())
    {
      birthYear = QString::number(*normalized.birthday->year);
    }
  }
  const QString trainingStartYear =
      normalized.trainingStartMonth.has_value()
          ? QString::number(normalized.trainingStartMonth->year)
          : "NULL";
  const QString trainingStartMonth =
      normalized.trainingStartMonth.has_value()
          ? QString::number(normalized.trainingStartMonth->month)
          : "NULL";
  QList<QString> sql = {
      "PRAGMA foreign_keys = ON", "BEGIN",
      QString("INSERT INTO participants(id, display_name, historical_name, "
              "full_name, contact, birth_day, birth_month, birth_year, rank, "
              "training_start_year, training_start_month, notes) VALUES(%1, "
              "%2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12) ON "
              "CONFLICT(id) DO NOTHING")
          .arg(sqlQuote(normalized.id.value))
          .arg(sqlQuote(normalized.displayName))
          .arg(sqlQuote(normalized.historicalName))
          .arg(sqlQuote(normalized.fullName))
          .arg(sqlQuote(normalized.contact))
          .arg(birthDay)
          .arg(birthMonth)
          .arg(birthYear)
          .arg(sqlQuote(ParticipantRankStorageValue(normalized.rank)))
          .arg(trainingStartYear)
          .arg(trainingStartMonth)
          .arg(sqlQuote(normalized.notes)),
      QString("INSERT OR IGNORE INTO month_participants(year, month, "
              "participant_id, sort_order) VALUES(%1, %2, %3, "
              "COALESCE((SELECT MAX(sort_order) + 1 FROM month_participants "
              "WHERE year = %1 AND month = %2), 0))")
          .arg(year)
          .arg(month)
          .arg(sqlQuote(normalized.id.value))};
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
            .arg(sqlQuote(normalized.id.value)));
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
    if (!IsValidParticipantSnapshot(participant) ||
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
        QString("INSERT INTO participants(id, display_name, historical_name, "
                "full_name) VALUES(%1, %2, %3, %4) ON CONFLICT(id) DO "
                "NOTHING")
            .arg(sqlQuote(participant.id.value))
            .arg(sqlQuote(participant.displayName))
            .arg(sqlQuote(participant.historicalName))
            .arg(sqlQuote(participant.fullName)));
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
      "length(NEW.display_name) > 300 OR length(NEW.notes) > 4096 OR NOT ("
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
  // DEFAULT ' ' позволяет добавить NOT NULL колонку без rebuild; этот trigger
  // отвергает пробел как пустое вычисленное имя.
  const QString currentProfileValidation =
      detailedProfileValidation +
      " OR length(NEW.historical_name) > 200 OR "
      "instr(NEW.historical_name, char(10)) != 0 OR "
      "instr(NEW.historical_name, char(13)) != 0 OR "
      "(length(trim(NEW.historical_name)) = 0 AND "
      "length(trim(NEW.full_name)) = 0) OR trim(NEW.display_name) != CASE "
      "WHEN length(trim(NEW.historical_name)) > 0 THEN "
      "trim(NEW.historical_name) ELSE trim(NEW.full_name) END";
  const QString currentInsertTrigger =
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(currentProfileValidation);
  const QString currentUpdateTrigger =
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes, rank, "
              "full_name, contact, historical_name ON participants WHEN %1 "
              "BEGIN SELECT RAISE(ABORT, 'invalid participant profile'); END")
          .arg(currentProfileValidation);
  const QString trainingProfileValidation =
      currentProfileValidation +
      " OR (NEW.training_start_year IS NULL) != "
      "(NEW.training_start_month IS NULL) OR "
      "(NEW.training_start_year IS NOT NULL AND "
      "(NEW.training_start_year NOT BETWEEN 1900 AND 9999 OR "
      "NEW.training_start_month NOT BETWEEN 1 AND 12))";
  const QString trainingInsertTrigger =
      QString("CREATE TRIGGER participants_profile_insert BEFORE INSERT ON "
              "participants WHEN %1 BEGIN SELECT RAISE(ABORT, 'invalid "
              "participant profile'); END")
          .arg(trainingProfileValidation);
  const QString trainingUpdateTrigger =
      QString("CREATE TRIGGER participants_profile_update BEFORE UPDATE OF "
              "display_name, birth_day, birth_month, birth_year, notes, rank, "
              "full_name, contact, historical_name, training_start_year, "
              "training_start_month ON participants WHEN %1 BEGIN SELECT "
              "RAISE(ABORT, 'invalid participant profile'); END")
          .arg(trainingProfileValidation);

  if (!tables.contains("journal_schema"))
  {
    if (!options_.allowSchemaChanges)
    {
      return fail("Remote schema is not initialized; run explicit bootstrap");
    }
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
        "TEXT NOT NULL DEFAULT ' ' "
        "CHECK(length(display_name) BETWEEN 1 AND 300), "
        "birth_day INTEGER CHECK(birth_day BETWEEN 1 AND 31), birth_month "
        "INTEGER CHECK(birth_month BETWEEN 1 AND 12), birth_year INTEGER "
        "CHECK(birth_year BETWEEN 1 AND 9999), notes TEXT NOT NULL DEFAULT '' "
        "CHECK(length(notes) <= 4096), rank TEXT NOT NULL DEFAULT 'guest' "
        "CHECK(rank IN ('page', 'squire', 'novice', 'recruit', 'guest', "
        "'knight')), full_name TEXT NOT NULL DEFAULT '' "
        "CHECK(length(full_name) <= 300 AND instr(full_name, char(10)) = 0 "
        "AND instr(full_name, char(13)) = 0), contact TEXT NOT NULL DEFAULT '' "
        "CHECK(length(contact) <= 500 AND instr(contact, char(10)) = 0 AND "
        "instr(contact, char(13)) = 0), historical_name TEXT NOT NULL "
        "DEFAULT '' "
        "CHECK(length(historical_name) <= 200 AND "
        "instr(historical_name, char(10)) = 0 AND "
        "instr(historical_name, char(13)) = 0), training_start_year INTEGER "
        "CHECK(training_start_year BETWEEN 1900 AND 9999), "
        "training_start_month INTEGER "
        "CHECK(training_start_month BETWEEN 1 AND 12), created_at TEXT NOT "
        "NULL "
        "DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT "
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
        trainingInsertTrigger,
        trainingUpdateTrigger,
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
  if (version != kRemoteSchemaVersion && !options_.allowSchemaChanges)
  {
    return fail(QString("Remote schema version %1 requires an explicit "
                        "migration")
                    .arg(version));
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
            .arg(kRemoteParticipantDetailsSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteParticipantDetailsSchemaVersion;
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
      return fail(error.isEmpty() ? "Cannot verify development remote schema v6"
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
        "id",         "display_name", "birth_day",  "birth_month",
        "birth_year", "notes",        "rank",       "full_name",
        "contact",    "created_at",   "updated_at", "archived_at"};
    const QList<QPair<int, QSet<QString>>> expectedColumns = {
        {2, {"year", "month", "participant_id", "sort_order"}},
        {3, {"year", "month", "day", "participant_id", "is_checked"}},
        {4, {"year", "month", "day"}},
        {5, {"year", "month", "day", "participant_id", "kind_mask", "note"}}};
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
        markerSql.contains("kind_mask BETWEEN 1 AND 15", Qt::CaseInsensitive);
    const bool currentMarkerMask =
        markerSql.contains("kind_mask BETWEEN 1 AND 31", Qt::CaseInsensitive);
    if (requiredTables.size() != 6 || !columnsMatch ||
        !results.at(6).toObject().value("rows").toArray().isEmpty() ||
        results.at(7).toObject().value("rows").toArray().size() != 2 ||
        !results.at(8).toObject().value("rows").toArray().isEmpty() ||
        !results.at(9).toObject().value("rows").toArray().isEmpty() ||
        oldMarkerMask == currentMarkerMask ||
        !markerSql.contains("REFERENCES month_participants",
                            Qt::CaseInsensitive) ||
        !markerSql.contains("ON DELETE CASCADE", Qt::CaseInsensitive) ||
        !participantSql.contains("full_name TEXT NOT NULL DEFAULT '' "
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
         (!participantSql.contains("is_trainer INTEGER NOT NULL DEFAULT 0",
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
      migration.push_back("ALTER TABLE participants DROP COLUMN is_trainer");
    }
    if (oldMarkerMask)
    {
      migration.push_back("DROP INDEX IF EXISTS idx_day_markers_history");
      migration.push_back("ALTER TABLE participant_day_markers RENAME TO "
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
    migration.push_back(QString("UPDATE journal_schema SET version = %1")
                            .arg(kRemoteParticipantDetailsSchemaVersion));
    migration.push_back("COMMIT");
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteParticipantDetailsSchemaVersion;
  }
  if (version == kRemoteParticipantDetailsSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"PRAGMA table_info(participants)", "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')",
             "SELECT id FROM participants WHERE rank NOT IN ('page', "
             "'squire', 'novice', 'recruit', 'guest', 'knight') OR "
             "length(trim(display_name)) = 0 OR length(display_name) > 200 "
             "OR length(full_name) > 300 OR "
             "instr(full_name, char(10)) != 0 OR "
             "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
             "instr(contact, char(10)) != 0 OR "
             "instr(contact, char(13)) != 0 LIMIT 1",
             "SELECT sql FROM sqlite_master WHERE type = 'table' AND "
             "name = 'participants'"},
            &results, &error) ||
        results.size() != 5)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v7" : error);
    }
    QSet<QString> participantColumns;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      participantColumns.insert(cellString(value.toArray(), 1));
    }
    const QSet<QString> expectedParticipantColumns = {
        "id",         "display_name", "birth_day",  "birth_month",
        "birth_year", "notes",        "rank",       "full_name",
        "contact",    "created_at",   "updated_at", "archived_at"};
    const QJsonArray participantDefinitionRows =
        results.at(4).toObject().value("rows").toArray();
    const QString participantSql =
        participantDefinitionRows.isEmpty()
            ? QString()
            : cellString(participantDefinitionRows.at(0).toArray(), 0)
                  .simplified();
    if (participantColumns != expectedParticipantColumns ||
        !results.at(1).toObject().value("rows").toArray().isEmpty() ||
        results.at(2).toObject().value("rows").toArray().size() != 2 ||
        !results.at(3).toObject().value("rows").toArray().isEmpty() ||
        !participantSql.contains("full_name TEXT NOT NULL DEFAULT '' "
                                 "CHECK(length(full_name) <= 300 AND "
                                 "instr(full_name, char(10)) = 0 AND "
                                 "instr(full_name, char(13)) = 0)",
                                 Qt::CaseInsensitive) ||
        !participantSql.contains(
            "contact TEXT NOT NULL DEFAULT '' CHECK(length(contact) <= 500 "
            "AND instr(contact, char(10)) = 0 AND "
            "instr(contact, char(13)) = 0)",
            Qt::CaseInsensitive))
    {
      return fail("Remote schema v7 is not a known shape");
    }
    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "DROP TRIGGER participants_profile_insert",
        "DROP TRIGGER participants_profile_update",
        "ALTER TABLE participants RENAME COLUMN display_name TO "
        "legacy_display_name",
        "ALTER TABLE participants ADD COLUMN display_name TEXT NOT NULL "
        "DEFAULT ' ' CHECK(length(display_name) BETWEEN 1 AND 300)",
        "ALTER TABLE participants ADD COLUMN historical_name TEXT NOT NULL "
        "DEFAULT '' CHECK(length(historical_name) <= 200 AND "
        "instr(historical_name, char(10)) = 0 AND "
        "instr(historical_name, char(13)) = 0)",
        "UPDATE participants SET display_name = trim(legacy_display_name), "
        "historical_name = trim(legacy_display_name)",
        "ALTER TABLE participants DROP COLUMN legacy_display_name",
        currentInsertTrigger,
        currentUpdateTrigger,
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteParticipantNameSchemaVersion),
        "COMMIT"};
    if (!executePipeline(migration, nullptr, &error))
    {
      return fail(error);
    }
    version = kRemoteParticipantNameSchemaVersion;
  }
  if (version == kRemoteParticipantNameSchemaVersion)
  {
    results = {};
    if (!executePipeline(
            {"PRAGMA table_info(participants)", "PRAGMA foreign_key_check",
             "SELECT name FROM sqlite_master WHERE type = 'trigger' AND "
             "name IN ('participants_profile_insert', "
             "'participants_profile_update')",
             "SELECT id FROM participants WHERE "
             "length(display_name) > 300 OR "
             "length(trim(display_name)) = 0 OR length(full_name) > 300 OR "
             "instr(full_name, char(10)) != 0 OR "
             "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
             "instr(contact, char(10)) != 0 OR "
             "instr(contact, char(13)) != 0 OR "
             "length(historical_name) > 200 OR "
             "instr(historical_name, char(10)) != 0 OR "
             "instr(historical_name, char(13)) != 0 OR "
             "(length(trim(historical_name)) = 0 AND "
             "length(trim(full_name)) = 0) OR trim(display_name) != CASE "
             "WHEN length(trim(historical_name)) > 0 THEN "
             "trim(historical_name) ELSE trim(full_name) END LIMIT 1"},
            &results, &error) ||
        results.size() != 4)
    {
      return fail(error.isEmpty() ? "Cannot verify remote schema v8" : error);
    }
    QSet<QString> participantColumns;
    for (const QJsonValue& value :
         results.at(0).toObject().value("rows").toArray())
    {
      participantColumns.insert(cellString(value.toArray(), 1));
    }
    const QSet<QString> expectedParticipantColumns = {
        "id",         "display_name", "birth_day",  "birth_month",
        "birth_year", "notes",        "rank",       "full_name",
        "contact",    "historical_name", "created_at", "updated_at",
        "archived_at"};
    if (participantColumns != expectedParticipantColumns ||
        !results.at(1).toObject().value("rows").toArray().isEmpty() ||
        results.at(2).toObject().value("rows").toArray().size() != 2 ||
        !results.at(3).toObject().value("rows").toArray().isEmpty())
    {
      return fail("Remote schema v8 is not a known shape");
    }
    const QList<QString> migration = {
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        "DROP TRIGGER participants_profile_insert",
        "DROP TRIGGER participants_profile_update",
        "ALTER TABLE participants ADD COLUMN training_start_year INTEGER "
        "CHECK(training_start_year BETWEEN 1900 AND 9999)",
        "ALTER TABLE participants ADD COLUMN training_start_month INTEGER "
        "CHECK(training_start_month BETWEEN 1 AND 12)",
        trainingInsertTrigger,
        trainingUpdateTrigger,
        QString("UPDATE journal_schema SET version = %1")
            .arg(kRemoteSchemaVersion),
        "COMMIT"};
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
           "SELECT name, sql FROM sqlite_master WHERE type = 'trigger' AND "
           "name IN ('participants_profile_insert', "
           "'participants_profile_update')",
           "SELECT id FROM participants WHERE rank NOT IN ('page', 'squire', "
           "'novice', 'recruit', 'guest', 'knight') OR "
           "length(display_name) > 300 OR "
           "length(trim(display_name)) = 0 OR length(full_name) > 300 OR "
           "instr(full_name, char(10)) != 0 OR "
           "instr(full_name, char(13)) != 0 OR length(contact) > 500 OR "
           "instr(contact, char(10)) != 0 OR "
           "instr(contact, char(13)) != 0 OR length(historical_name) > 200 "
           "OR instr(historical_name, char(10)) != 0 OR "
           "instr(historical_name, char(13)) != 0 OR "
           "(length(trim(historical_name)) = 0 AND "
           "length(trim(full_name)) = 0) OR trim(display_name) != CASE WHEN "
           "length(trim(historical_name)) > 0 THEN trim(historical_name) "
           "ELSE trim(full_name) END OR "
           "(training_start_year IS NULL) != "
           "(training_start_month IS NULL) OR "
           "(training_start_year IS NOT NULL AND "
           "(training_start_year NOT BETWEEN 1900 AND 9999 OR "
           "training_start_month NOT BETWEEN 1 AND 12)) LIMIT 1",
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
        "rank", "full_name", "contact", "historical_name",
        "training_start_year", "training_start_month", "created_at",
        "updated_at", "archived_at"}},
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
      return fail("Remote schema v9 columns are incomplete");
    }
    if (entry.first == 1 && columns.contains("is_trainer"))
    {
      return fail("Remote schema v9 contains obsolete trainer column");
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
  bool trainingTriggersValid = true;
  const QJsonArray triggerRows =
      results.at(7).toObject().value("rows").toArray();
  for (const QJsonValue& value : triggerRows)
  {
    const QString triggerSql = cellString(value.toArray(), 1).simplified();
    trainingTriggersValid =
        trainingTriggersValid &&
        triggerSql.contains("NEW.training_start_year IS NULL",
                            Qt::CaseInsensitive) &&
        triggerSql.contains("NEW.training_start_month IS NULL",
                            Qt::CaseInsensitive);
  }
  if (requiredTables.size() != 6 ||
      !results.at(6).toObject().value("rows").toArray().isEmpty() ||
      triggerRows.size() != 2 || !trainingTriggersValid ||
      !results.at(8).toObject().value("rows").toArray().isEmpty() ||
      !results.at(9).toObject().value("rows").toArray().isEmpty() ||
      !markerSql.contains("kind_mask BETWEEN 1 AND 31", Qt::CaseInsensitive) ||
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
      !participantSql.contains("historical_name TEXT NOT NULL DEFAULT '' "
                               "CHECK(length(historical_name) <= 200 AND "
                               "instr(historical_name, char(10)) = 0 AND "
                               "instr(historical_name, char(13)) = 0)",
                               Qt::CaseInsensitive) ||
      !participantSql.contains(
          "display_name TEXT NOT NULL DEFAULT ' ' "
          "CHECK(length(display_name) BETWEEN 1 AND 300)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "training_start_year INTEGER CHECK(training_start_year BETWEEN "
          "1900 AND 9999)",
          Qt::CaseInsensitive) ||
      !participantSql.contains(
          "training_start_month INTEGER CHECK(training_start_month BETWEEN "
          "1 AND 12)",
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
  // BEGIN/COMMIT превращаются в один Hrana batch с condition chain. Без этой
  // цепочки HTTP 200 мог бы скрыть частично выполненную запись месяца.
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
  QNetworkRequest request(QUrl(options_.baseUrl + "/v2/pipeline"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  // Redirect запрещён: иначе bearer token может уйти другому origin.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  if (!options_.authToken.isEmpty())
  {
    request.setRawHeader("Authorization",
                         "Bearer " + options_.authToken.toUtf8());
  }
  QNetworkReply* reply = network_.post(
      request, QJsonDocument(root).toJson(QJsonDocument::Compact));
  // Вложенный loop оставлен только для PoC совместимости. Он допускает Qt
  // reentrancy и блокирует сценарий до timeout; production transport должен
  // возвращать async result с cancellation.
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(options_.timeoutMs);
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
  const int httpStatus =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (httpStatus < 200 || httpStatus >= 300)
  {
    if (errorMessage)
    {
      *errorMessage = QString("Remote HTTP status %1").arg(httpStatus);
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
  if (row.size() < 13)
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
  profile.historicalName = cellString(row, 10);
  const auto trainingStartYear = cellOptionalInt(row, 11);
  const auto trainingStartMonth = cellOptionalInt(row, 12);
  if (trainingStartYear.has_value() != trainingStartMonth.has_value())
  {
    return std::nullopt;
  }
  if (trainingStartYear.has_value())
  {
    profile.trainingStartMonth =
        JournalMonth{*trainingStartYear, *trainingStartMonth};
  }
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
              "rank, notes, archived_at IS NOT NULL, full_name, contact, "
              "historical_name, training_start_year, training_start_month "
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

std::optional<ParticipantJournalStatistics>
JournalRemote::participantStatistics(const ParticipantId& id)
{
  lastError_.clear();
  auto fail = [this](const QString& error)
      -> std::optional<ParticipantJournalStatistics>
  {
    lastError_ = error;
    return std::nullopt;
  };
  if (!id.isValid())
  {
    return fail("Invalid participant ID");
  }
  auto monthKey = [](int year, int month)
  {
    return static_cast<qint64>(year) * 100 + month;
  };
  auto dateKey = [&monthKey](int year, int month, int day)
  {
    return monthKey(year, month) * 100 + day;
  };

  const QString quotedId = sqlQuote(id.value);
  const QList<QString> sql = {
      "BEGIN",
      QString("SELECT EXISTS(SELECT 1 FROM participants WHERE id = %1)")
          .arg(quotedId),
      QString("SELECT year, month FROM month_participants WHERE "
              "participant_id = %1 ORDER BY year, month")
          .arg(quotedId),
      QString("SELECT md.year, md.month, md.day FROM month_days md "
              "JOIN month_participants mp ON mp.year = md.year AND "
              "mp.month = md.month WHERE mp.participant_id = %1 "
              "ORDER BY md.year, md.month, md.day")
          .arg(quotedId),
      QString("SELECT year, month, day FROM attendance WHERE "
              "participant_id = %1 AND is_checked = 1 "
              "ORDER BY year, month, day")
          .arg(quotedId),
      QString("SELECT year, month, day, kind_mask FROM "
              "participant_day_markers WHERE participant_id = %1 "
              "ORDER BY year, month, day")
          .arg(quotedId),
      "COMMIT"};
  QJsonArray results;
  QString error;
  if (!executePipeline(sql, &results, &error) || results.size() != 5)
  {
    return fail(error.isEmpty() ? "Invalid remote participant statistics"
                                : error);
  }

  const QJsonArray participantRows =
      results.at(0).toObject().value("rows").toArray();
  if (participantRows.size() != 1 ||
      cellString(participantRows.at(0).toArray(), 0) != "1")
  {
    return fail("Remote participant not found");
  }

  ParticipantJournalStatistics result{id, {}, 0, 0, 0, std::nullopt,
                                      std::nullopt};
  QHash<qint64, int> monthIndexes;
  for (const QJsonValue& value :
       results.at(1).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool yearOk = false;
    bool monthOk = false;
    const int year = cellString(row, 0).toInt(&yearOk);
    const int month = cellString(row, 1).toInt(&monthOk);
    const qint64 key = monthKey(year, month);
    if (row.size() < 2 || !yearOk || !monthOk ||
        !QDate(year, month, 1).isValid() || monthIndexes.contains(key))
    {
      return fail("Invalid remote participant month");
    }
    monthIndexes.insert(key, static_cast<int>(result.months.size()));
    result.months.push_back({{year, month}, 0, 0, 0, 0});
  }

  QHash<qint64, QSet<int>> activeDaysByMonth;
  for (const QJsonValue& value :
       results.at(2).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool yearOk = false;
    bool monthOk = false;
    bool dayOk = false;
    const int year = cellString(row, 0).toInt(&yearOk);
    const int month = cellString(row, 1).toInt(&monthOk);
    const int day = cellString(row, 2).toInt(&dayOk);
    const qint64 key = monthKey(year, month);
    QSet<int>& days = activeDaysByMonth[key];
    if (row.size() < 3 || !yearOk || !monthOk || !dayOk ||
        !monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        days.contains(day))
    {
      return fail("Invalid remote participant active day");
    }
    days.insert(day);
  }
  for (ParticipantMonthStatistics& month : result.months)
  {
    const qint64 key = monthKey(month.month.year, month.month.month);
    const QSet<int> days = activeDaysByMonth.value(key);
    month.trackedDayCount =
        days.isEmpty()
            ? QDate(month.month.year, month.month.month, 1).daysInMonth()
            : days.size();
  }

  QSet<qint64> checkedDays;
  for (const QJsonValue& value :
       results.at(3).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool yearOk = false;
    bool monthOk = false;
    bool dayOk = false;
    const int year = cellString(row, 0).toInt(&yearOk);
    const int month = cellString(row, 1).toInt(&monthOk);
    const int day = cellString(row, 2).toInt(&dayOk);
    const qint64 key = monthKey(year, month);
    const qint64 checkedKey = dateKey(year, month, day);
    const QSet<int> activeDays = activeDaysByMonth.value(key);
    if (row.size() < 3 || !yearOk || !monthOk || !dayOk ||
        !monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        checkedDays.contains(checkedKey))
    {
      return fail("Invalid remote participant attendance");
    }
    if (!activeDays.isEmpty() && !activeDays.contains(day))
    {
      continue;
    }
    checkedDays.insert(checkedKey);
    ParticipantMonthStatistics& statistics =
        result.months.at(monthIndexes.value(key));
    ++statistics.attendedDayCount;
    ++result.totalAttendedDayCount;
    const QDate date(year, month, day);
    if (!result.firstRecordedVisit.has_value() ||
        date < *result.firstRecordedVisit)
    {
      result.firstRecordedVisit = date;
    }
    if (!result.lastRecordedVisit.has_value() ||
        date > *result.lastRecordedVisit)
    {
      result.lastRecordedVisit = date;
    }
  }

  QSet<qint64> markerDays;
  for (const QJsonValue& value :
       results.at(4).toObject().value("rows").toArray())
  {
    const QJsonArray row = value.toArray();
    bool yearOk = false;
    bool monthOk = false;
    bool dayOk = false;
    bool kindsOk = false;
    const int year = cellString(row, 0).toInt(&yearOk);
    const int month = cellString(row, 1).toInt(&monthOk);
    const int day = cellString(row, 2).toInt(&dayOk);
    const int kindsValue = cellString(row, 3).toInt(&kindsOk);
    const auto kinds = DayMarkerKindsFromInt(kindsValue);
    const qint64 key = monthKey(year, month);
    const qint64 markerKey = dateKey(year, month, day);
    const QSet<int> activeDays = activeDaysByMonth.value(key);
    if (row.size() < 4 || !yearOk || !monthOk || !dayOk || !kindsOk ||
        !monthIndexes.contains(key) || !QDate(year, month, day).isValid() ||
        !kinds.has_value() || markerDays.contains(markerKey))
    {
      return fail("Invalid remote participant day marker");
    }
    markerDays.insert(markerKey);
    if (!activeDays.isEmpty() && !activeDays.contains(day))
    {
      continue;
    }
    ParticipantMonthStatistics& statistics =
        result.months.at(monthIndexes.value(key));
    if (kinds->testFlag(DayMarkerKind::SpecialTraining) &&
        checkedDays.contains(markerKey))
    {
      ++statistics.specialTrainingVisitCount;
      ++result.totalSpecialTrainingVisitCount;
    }
    if (kinds->testFlag(DayMarkerKind::LedTraining))
    {
      ++statistics.ledTrainingDayCount;
      ++result.totalLedTrainingDayCount;
    }
  }
  return result;
}

std::optional<ParticipantEmblem>
JournalRemote::getParticipantEmblem(const ParticipantId&)
{
  lastError_ =
      "Remote participant emblems are unavailable without asset transport";
  return std::nullopt;
}

bool JournalRemote::updateParticipantCard(const ParticipantCardUpdate&)
{
  lastError_ =
      "Remote participant emblems are unavailable without asset transport";
  return false;
}

std::optional<std::vector<TimedStrikeTest>>
JournalRemote::timedStrikeTests(const ParticipantId&)
{
  lastError_ = "Remote timed strike tests are not implemented";
  return std::nullopt;
}

bool JournalRemote::saveTimedStrikeTest(const TimedStrikeTest&)
{
  lastError_ = "Remote timed strike tests are not implemented";
  return false;
}

bool JournalRemote::removeTimedStrikeTest(const TimedStrikeTestId&, qint64)
{
  lastError_ = "Remote timed strike tests are not implemented";
  return false;
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
      "notes, archived_at IS NOT NULL, full_name, contact, historical_name, "
      "training_start_year, training_start_month "
      "FROM participants " +
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
  normalized.historicalName = normalized.historicalName.trimmed();
  normalized.fullName = normalized.fullName.trimmed();
  normalized.contact = normalized.contact.trimmed();
  normalized.displayName = ParticipantDisplayName(normalized);
  if ((normalized.historicalName.isEmpty() && normalized.fullName.isEmpty()) ||
      !normalized.isValid() ||
      !IsTrainingStartMonthNotAfter(normalized.trainingStartMonth,
                                    QDate::currentDate()))
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
  const QString trainingStartYear =
      normalized.trainingStartMonth.has_value()
          ? QString::number(normalized.trainingStartMonth->year)
          : "NULL";
  const QString trainingStartMonth =
      normalized.trainingStartMonth.has_value()
          ? QString::number(normalized.trainingStartMonth->month)
          : "NULL";
  const QString sql =
      QString("UPDATE participants SET display_name = %1, birth_day = %2, "
              "birth_month = %3, birth_year = %4, rank = %5, notes = %6, "
              "historical_name = %7, full_name = %8, contact = %9, "
              "training_start_year = %10, training_start_month = %11, "
              "updated_at = CURRENT_TIMESTAMP WHERE id = %12")
          .arg(sqlQuote(normalized.displayName))
          .arg(day)
          .arg(month)
          .arg(year)
          .arg(sqlQuote(ParticipantRankStorageValue(normalized.rank)))
          .arg(sqlQuote(normalized.notes))
          .arg(sqlQuote(normalized.historicalName))
          .arg(sqlQuote(normalized.fullName))
          .arg(sqlQuote(normalized.contact))
          .arg(trainingStartYear)
          .arg(trainingStartMonth)
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
