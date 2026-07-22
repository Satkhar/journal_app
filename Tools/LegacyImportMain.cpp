#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTextStream>
#include <QUuid>

#include <optional>
#include <vector>

#include "SqliteConnect.hpp"

namespace
{

struct ImportMonth
{
  int year = 0;
  int month = 0;
  MonthSnapshot snapshot;
};

struct ImportContractV2
{
  std::vector<ParticipantProfile> profiles;
  std::vector<ImportMonth> months;
};

bool fail(const QString& message)
{
  QTextStream(stderr) << message << Qt::endl;
  return false;
}

std::optional<QJsonObject> readContract(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    fail(QString("Не удалось открыть JSON: %1").arg(file.errorString()));
    return std::nullopt;
  }
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
  {
    fail(QString("Некорректный JSON: %1").arg(error.errorString()));
    return std::nullopt;
  }
  return document.object();
}

std::optional<int> jsonInt(const QJsonValue& value)
{
  if (!value.isDouble())
  {
    return std::nullopt;
  }
  const double number = value.toDouble();
  const int result = value.toInt();
  return number == result ? std::optional<int>(result) : std::nullopt;
}

std::optional<QDate> optionalDate(const QJsonValue& value, bool* valid)
{
  *valid = false;
  if (value.isNull() || value.isUndefined())
  {
    *valid = true;
    return std::nullopt;
  }
  if (!value.isString())
  {
    return std::nullopt;
  }
  const QDate result = QDate::fromString(value.toString(), Qt::ISODate);
  if (!result.isValid())
  {
    return std::nullopt;
  }
  *valid = true;
  return result;
}

bool parseProfileV2(const QJsonValue& value, ParticipantProfile* profile)
{
  if (!profile || !value.isObject())
  {
    return fail("JSON v2 содержит некорректный профиль");
  }
  const QJsonObject object = value.toObject();
  if (!object.value("id").isString() ||
      !object.value("historical_name").isString() ||
      !object.value("full_name").isString() ||
      !object.value("contact").isString() ||
      !object.value("rank").isString() ||
      !object.value("combat_hand").isString() ||
      !object.value("notes").isString() ||
      !object.value("rank_history").isArray())
  {
    return fail("JSON v2 содержит неполный профиль");
  }

  profile->id = {object.value("id").toString()};
  profile->historicalName = object.value("historical_name").toString();
  profile->fullName = object.value("full_name").toString();
  profile->displayName = ParticipantDisplayName(*profile);
  profile->contact = object.value("contact").toString();
  profile->notes = object.value("notes").toString();
  const auto rank =
      ParticipantRankFromStorageValue(object.value("rank").toString());
  const auto hand =
      CombatHandFromStorageValue(object.value("combat_hand").toString());
  if (!rank || !hand)
  {
    return fail("JSON v2 содержит неизвестное звание или боевую руку");
  }
  profile->rank = *rank;
  profile->combatHand = *hand;

  const QJsonValue birthdayValue = object.value("birthday");
  if (!birthdayValue.isNull() && !birthdayValue.isUndefined())
  {
    if (!birthdayValue.isObject())
    {
      return fail("JSON v2 содержит некорректный день рождения");
    }
    const QJsonObject birthday = birthdayValue.toObject();
    const auto day = jsonInt(birthday.value("day"));
    const auto month = jsonInt(birthday.value("month"));
    if (!day || !month)
    {
      return fail("JSON v2 содержит некорректный день рождения");
    }
    Birthday parsed{*day, *month, std::nullopt};
    const QJsonValue yearValue = birthday.value("year");
    if (!yearValue.isNull() && !yearValue.isUndefined())
    {
      const auto year = jsonInt(yearValue);
      if (!year)
      {
        return fail("JSON v2 содержит некорректный год рождения");
      }
      parsed.year = *year;
    }
    profile->birthday = parsed;
  }

  const QJsonValue trainingValue = object.value("training_start_month");
  if (!trainingValue.isNull() && !trainingValue.isUndefined())
  {
    if (!trainingValue.isObject())
    {
      return fail("JSON v2 содержит некорректный месяц начала тренировок");
    }
    const QJsonObject training = trainingValue.toObject();
    const auto year = jsonInt(training.value("year"));
    const auto month = jsonInt(training.value("month"));
    if (!year || !month)
    {
      return fail("JSON v2 содержит некорректный месяц начала тренировок");
    }
    profile->trainingStartMonth = JournalMonth{*year, *month};
  }

  bool validDate = false;
  profile->joinedClubOn =
      optionalDate(object.value("joined_club_on"), &validDate);
  if (!validDate)
  {
    return fail("JSON v2 содержит некорректную дату вступления в клуб");
  }

  QSet<int> historyRanks;
  for (const QJsonValue& historyValue :
       object.value("rank_history").toArray())
  {
    if (!historyValue.isObject())
    {
      return fail("JSON v2 содержит некорректную историю званий");
    }
    const QJsonObject historyObject = historyValue.toObject();
    const auto historyRank = ParticipantRankFromStorageValue(
        historyObject.value("rank").toString());
    if (!historyRank || !IsParticipantRankWithHistory(*historyRank) ||
        historyRanks.contains(static_cast<int>(*historyRank)))
    {
      return fail("JSON v2 содержит повтор или неизвестное звание в истории");
    }
    bool validObtainedOn = false;
    const auto obtainedOn =
        optionalDate(historyObject.value("obtained_on"), &validObtainedOn);
    if (!validObtainedOn)
    {
      return fail("JSON v2 содержит некорректную дату получения звания");
    }
    historyRanks.insert(static_cast<int>(*historyRank));
    profile->rankHistory.push_back({*historyRank, obtainedOn});
  }
  profile->archived = false;
  if (!profile->isValid() ||
      !IsTrainingStartMonthNotAfter(profile->trainingStartMonth,
                                    QDate::currentDate()) ||
      !AreParticipantMilestoneDatesNotAfter(*profile, QDate::currentDate()))
  {
    return fail(QString("JSON v2 содержит невалидный профиль: %1")
                    .arg(profile->id.value));
  }
  return true;
}

std::optional<ImportContractV2> parseContractV2(const QJsonObject& root)
{
  if (!root.value("participants").isArray() ||
      !root.value("months").isArray())
  {
    fail("JSON v2 не содержит profiles/months");
    return std::nullopt;
  }

  ImportContractV2 result;
  QHash<QString, ParticipantProfile> profilesById;
  for (const QJsonValue& value : root.value("participants").toArray())
  {
    ParticipantProfile profile;
    if (!parseProfileV2(value, &profile))
    {
      return std::nullopt;
    }
    if (profilesById.contains(profile.id.value))
    {
      fail(QString("JSON v2 повторяет UUID профиля: %1")
               .arg(profile.id.value));
      return std::nullopt;
    }
    profilesById.insert(profile.id.value, profile);
    result.profiles.push_back(profile);
  }
  if (result.profiles.empty())
  {
    fail("JSON v2 не содержит профилей");
    return std::nullopt;
  }

  QSet<QString> monthKeys;
  QSet<QString> usedProfileIds;
  for (const QJsonValue& value : root.value("months").toArray())
  {
    if (!value.isObject())
    {
      fail("JSON v2 содержит некорректный месяц");
      return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const auto year = jsonInt(object.value("year"));
    const auto month = jsonInt(object.value("month"));
    if (!year || !month || !QDate(*year, *month, 1).isValid() ||
        !object.value("active_days").isArray() ||
        !object.value("participant_ids").isArray() ||
        !object.value("attendance").isArray() ||
        !object.value("day_markers").isArray())
    {
      fail("JSON v2 содержит неполный месяц");
      return std::nullopt;
    }
    const QString monthKey =
        QString("%1-%2").arg(*year).arg(*month, 2, 10, QLatin1Char('0'));
    if (monthKeys.contains(monthKey))
    {
      fail(QString("JSON v2 повторяет месяц: %1").arg(monthKey));
      return std::nullopt;
    }
    monthKeys.insert(monthKey);

    ImportMonth parsed;
    parsed.year = *year;
    parsed.month = *month;
    parsed.snapshot.state = MonthState::Ready;
    QSet<int> activeDaySet;
    int previousDay = 0;
    for (const QJsonValue& dayValue : object.value("active_days").toArray())
    {
      const auto day = jsonInt(dayValue);
      if (!day || !QDate(*year, *month, *day).isValid() ||
          activeDaySet.contains(*day) || *day <= previousDay)
      {
        fail(QString("JSON v2 содержит некорректные даты месяца %1")
                 .arg(monthKey));
        return std::nullopt;
      }
      previousDay = *day;
      activeDaySet.insert(*day);
      parsed.snapshot.activeDays.push_back(*day);
    }
    if (parsed.snapshot.activeDays.isEmpty())
    {
      fail(QString("JSON v2 содержит пустой календарь месяца %1")
               .arg(monthKey));
      return std::nullopt;
    }

    QSet<QString> memberIds;
    std::vector<ParticipantId> orderedIds;
    for (const QJsonValue& idValue : object.value("participant_ids").toArray())
    {
      if (!idValue.isString() || memberIds.contains(idValue.toString()) ||
          !profilesById.contains(idValue.toString()))
      {
        fail(QString("JSON v2 содержит неизвестного или повторного участника "
                     "месяца %1")
                 .arg(monthKey));
        return std::nullopt;
      }
      const ParticipantProfile& profile = profilesById[idValue.toString()];
      memberIds.insert(profile.id.value);
      usedProfileIds.insert(profile.id.value);
      orderedIds.push_back(profile.id);
      parsed.snapshot.participants.push_back(
          {profile.id, profile.displayName, profile.historicalName,
           profile.fullName});
    }

    QSet<QString> checkedAttendance;
    for (const QJsonValue& attendanceValue :
         object.value("attendance").toArray())
    {
      if (!attendanceValue.isObject())
      {
        fail("JSON v2 содержит некорректное посещение");
        return std::nullopt;
      }
      const QJsonObject attendanceObject = attendanceValue.toObject();
      const QString id = attendanceObject.value("participant_id").toString();
      const auto day = jsonInt(attendanceObject.value("day"));
      const QString key = id + ':' + QString::number(day.value_or(0));
      if (!memberIds.contains(id) || !day || !activeDaySet.contains(*day) ||
          checkedAttendance.contains(key))
      {
        fail(QString("JSON v2 содержит некорректное посещение месяца %1")
                 .arg(monthKey));
        return std::nullopt;
      }
      checkedAttendance.insert(key);
    }
    // A full matrix preserves the storage invariant that every visible cell
    // has an explicit value; the compact JSON lists checked cells only.
    for (const ParticipantId& id : orderedIds)
    {
      for (int day : parsed.snapshot.activeDays)
      {
        const QString key = id.value + ':' + QString::number(day);
        parsed.snapshot.attendance.push_back(
            {id, day, checkedAttendance.contains(key)});
      }
    }

    QSet<QString> markerKeys;
    for (const QJsonValue& markerValue :
         object.value("day_markers").toArray())
    {
      if (!markerValue.isObject())
      {
        fail("JSON v2 содержит некорректную отметку");
        return std::nullopt;
      }
      const QJsonObject markerObject = markerValue.toObject();
      const QString id = markerObject.value("participant_id").toString();
      const auto day = jsonInt(markerObject.value("day"));
      const auto mask = jsonInt(markerObject.value("kind_mask"));
      const QString key = id + ':' + QString::number(day.value_or(0));
      const auto kinds =
          mask.has_value() ? DayMarkerKindsFromInt(*mask) : std::nullopt;
      if (!memberIds.contains(id) || !day || !activeDaySet.contains(*day) ||
          !kinds || markerKeys.contains(key) ||
          !markerObject.value("note").isString())
      {
        fail(QString("JSON v2 содержит некорректную отметку месяца %1")
                 .arg(monthKey));
        return std::nullopt;
      }
      const QString note = markerObject.value("note").toString();
      if (note.size() > kMaxDayMarkerNoteLength)
      {
        fail(QString("JSON v2 содержит слишком длинную отметку месяца %1")
                 .arg(monthKey));
        return std::nullopt;
      }
      markerKeys.insert(key);
      parsed.snapshot.dayMarkers.push_back({{id}, *day, *kinds, note});
    }
    result.months.push_back(std::move(parsed));
  }

  if (result.months.empty() || usedProfileIds.size() != profilesById.size())
  {
    fail("JSON v2 содержит неиспользуемые профили или не содержит месяцев");
    return std::nullopt;
  }
  return result;
}

bool sameProfile(const ParticipantProfile& expected,
                 const ParticipantProfile& actual)
{
  if (expected.id != actual.id ||
      expected.displayName != actual.displayName ||
      expected.historicalName != actual.historicalName ||
      expected.fullName != actual.fullName ||
      expected.contact != actual.contact ||
      expected.birthday.has_value() != actual.birthday.has_value() ||
      !(expected.trainingStartMonth == actual.trainingStartMonth) ||
      expected.joinedClubOn != actual.joinedClubOn ||
      expected.rank != actual.rank ||
      expected.combatHand != actual.combatHand ||
      expected.notes != actual.notes || expected.archived != actual.archived ||
      expected.rankHistory.size() != actual.rankHistory.size())
  {
    return false;
  }
  if (expected.birthday.has_value())
  {
    const Birthday& lhs = *expected.birthday;
    const Birthday& rhs = *actual.birthday;
    if (lhs.day != rhs.day || lhs.month != rhs.month || lhs.year != rhs.year)
    {
      return false;
    }
  }
  QHash<QString, std::optional<QDate>> expectedHistory;
  for (const ParticipantRankHistoryEntry& entry : expected.rankHistory)
  {
    expectedHistory.insert(ParticipantRankStorageValue(entry.rank),
                           entry.obtainedOn);
  }
  for (const ParticipantRankHistoryEntry& entry : actual.rankHistory)
  {
    const QString rank = ParticipantRankStorageValue(entry.rank);
    if (!expectedHistory.contains(rank) ||
        expectedHistory.value(rank) != entry.obtainedOn)
    {
      return false;
    }
  }
  return true;
}

bool verifyContractV2(SqliteConnect& storage,
                      const ImportContractV2& contract)
{
  const auto storedProfiles = storage.listParticipantProfiles(true);
  if (!storedProfiles || storedProfiles->size() != contract.profiles.size())
  {
    return fail("Проверка импорта v2: число профилей не совпало");
  }
  QHash<QString, ParticipantProfile> expectedProfiles;
  for (const ParticipantProfile& profile : contract.profiles)
  {
    expectedProfiles.insert(profile.id.value, profile);
  }
  for (const ParticipantProfile& profile : *storedProfiles)
  {
    if (!expectedProfiles.contains(profile.id.value) ||
        !sameProfile(expectedProfiles.value(profile.id.value), profile))
    {
      return fail(QString("Проверка импорта v2: профиль не совпал: %1")
                      .arg(profile.id.value));
    }
  }

  const auto storedMonths = storage.listMonths();
  if (!storedMonths || storedMonths->size() != contract.months.size())
  {
    return fail("Проверка импорта v2: число месяцев не совпало");
  }
  for (const ImportMonth& expected : contract.months)
  {
    const MonthSnapshot actual =
        storage.loadMonthSnapshot(expected.year, expected.month);
    if (actual.state != MonthState::Ready ||
        actual.activeDays != expected.snapshot.activeDays ||
        actual.participants.size() != expected.snapshot.participants.size() ||
        actual.attendance.size() != expected.snapshot.attendance.size() ||
        actual.dayMarkers.size() != expected.snapshot.dayMarkers.size())
    {
      return fail(QString("Проверка импорта v2: месяц не совпал: %1-%2")
                      .arg(expected.year)
                      .arg(expected.month, 2, 10, QLatin1Char('0')));
    }
    for (std::size_t index = 0; index < actual.participants.size(); ++index)
    {
      const Participant& lhs = actual.participants[index];
      const Participant& rhs = expected.snapshot.participants[index];
      if (lhs.id != rhs.id || lhs.displayName != rhs.displayName ||
          lhs.historicalName != rhs.historicalName ||
          lhs.fullName != rhs.fullName)
      {
        return fail("Проверка импорта v2: порядок состава не совпал");
      }
    }
    QHash<QString, bool> expectedAttendance;
    for (const AttendanceRecord& record : expected.snapshot.attendance)
    {
      expectedAttendance.insert(record.participantId.value + ':' +
                                    QString::number(record.day),
                                record.isChecked);
    }
    for (const AttendanceRecord& record : actual.attendance)
    {
      const QString key =
          record.participantId.value + ':' + QString::number(record.day);
      if (!expectedAttendance.contains(key) ||
          expectedAttendance.value(key) != record.isChecked)
      {
        return fail("Проверка импорта v2: посещение не совпало");
      }
    }
    QHash<QString, ParticipantDayMarker> expectedMarkers;
    for (const ParticipantDayMarker& marker : expected.snapshot.dayMarkers)
    {
      expectedMarkers.insert(marker.participantId.value + ':' +
                                 QString::number(marker.day),
                             marker);
    }
    for (const ParticipantDayMarker& marker : actual.dayMarkers)
    {
      const QString key =
          marker.participantId.value + ':' + QString::number(marker.day);
      if (!expectedMarkers.contains(key))
      {
        return fail("Проверка импорта v2: отметка не найдена");
      }
      const ParticipantDayMarker& expectedMarker = expectedMarkers.value(key);
      if (marker.kinds != expectedMarker.kinds ||
          marker.note != expectedMarker.note)
      {
        return fail("Проверка импорта v2: отметка не совпала");
      }
    }
  }
  return true;
}

bool importContractV2(const ImportContractV2& contract,
                      const QString& databasePath)
{
  SqliteConnect storage;
  if (!storage.open(databasePath))
  {
    return fail(storage.lastError());
  }
  for (const ImportMonth& month : contract.months)
  {
    if (!storage.replaceMonth(month.year, month.month, month.snapshot))
    {
      return fail(storage.lastError());
    }
  }
  for (const ParticipantProfile& profile : contract.profiles)
  {
    if (!storage.updateParticipantProfile(profile))
    {
      return fail(storage.lastError());
    }
  }
  return verifyContractV2(storage, contract);
}

bool importContractV1(const QJsonObject& root, const QString& databasePath)
{
  if (!root.value("active_days").isArray() ||
      !root.value("participants").isArray() ||
      !root.value("attendance").isArray() ||
      !root.value("day_markers").isArray())
  {
    return fail("Неполный JSON-контракт v1");
  }
  const int year = root.value("year").toInt();
  const int month = root.value("month").toInt();
  QVector<int> activeDays;
  for (const QJsonValue& value : root.value("active_days").toArray())
  {
    activeDays.push_back(value.toInt());
  }

  SqliteConnect storage;
  if (!storage.open(databasePath) ||
      !storage.saveActiveDays(year, month, activeDays))
  {
    return fail(storage.lastError());
  }

  for (const QJsonValue& value : root.value("participants").toArray())
  {
    const QJsonObject object = value.toObject();
    ParticipantProfile profile;
    profile.id = {object.value("id").toString()};
    profile.historicalName = object.value("historical_name").toString();
    profile.fullName = object.value("full_name").toString();
    profile.displayName = ParticipantDisplayName(profile);
    profile.contact = object.value("contact").toString();
    profile.notes = object.value("notes").toString();
    const auto rank =
        ParticipantRankFromStorageValue(object.value("rank").toString());
    const auto hand =
        CombatHandFromStorageValue(object.value("combat_hand").toString());
    if (!rank || !hand)
    {
      return fail("JSON v1 содержит неизвестное звание или боевую руку");
    }
    profile.rank = *rank;
    profile.combatHand = *hand;
    if (!storage.addParticipantToMonth(year, month, profile))
    {
      return fail(storage.lastError());
    }
  }

  std::vector<AttendanceRecord> attendance;
  for (const QJsonValue& value : root.value("attendance").toArray())
  {
    const QJsonObject object = value.toObject();
    attendance.push_back({{object.value("participant_id").toString()},
                          object.value("day").toInt(), true});
  }
  if (!storage.saveAttendance(year, month, attendance))
  {
    return fail(storage.lastError());
  }

  for (const QJsonValue& value : root.value("day_markers").toArray())
  {
    const QJsonObject object = value.toObject();
    const auto kinds = DayMarkerKindsFromInt(object.value("kind_mask").toInt());
    if (!kinds)
    {
      return fail("JSON v1 содержит неизвестную комбинацию отметок");
    }
    const ParticipantDayMarker marker{
        {object.value("participant_id").toString()},
        object.value("day").toInt(), *kinds,
        object.value("note").toString()};
    if (!storage.saveDayMarker(year, month, marker))
    {
      return fail(storage.lastError());
    }
  }
  return true;
}

void removeTemporaryDatabase(const QString& path)
{
  QFile::remove(path);
  QFile::remove(path + "-journal");
  QFile::remove(path + "-wal");
  QFile::remove(path + "-shm");
}

bool importContract(const QJsonObject& root, const QString& databasePath)
{
  const auto version = jsonInt(root.value("format_version"));
  if (!version || (*version != 1 && *version != 2))
  {
    return fail("Неподдерживаемая версия JSON-контракта");
  }
  const QString temporaryPath =
      databasePath + ".importing-" +
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool imported = false;
  if (*version == 1)
  {
    imported = importContractV1(root, temporaryPath);
  }
  else
  {
    const auto parsed = parseContractV2(root);
    imported = parsed && importContractV2(*parsed, temporaryPath);
  }
  if (!imported)
  {
    removeTemporaryDatabase(temporaryPath);
    return false;
  }
  // Publishing by rename in the destination directory prevents consumers from
  // ever observing a partially written SQLite file.
  if (!QFile::rename(temporaryPath, databasePath))
  {
    removeTemporaryDatabase(temporaryPath);
    return fail("Не удалось опубликовать готовую БД");
  }
  return true;
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.setApplicationDescription("Импорт нормализованного старого журнала");
  parser.addHelpOption();
  parser.addPositionalArgument("json", "JSON-контракт импорта");
  parser.addPositionalArgument("database", "Новая SQLite БД");
  parser.process(app);
  const QStringList arguments = parser.positionalArguments();
  if (arguments.size() != 2)
  {
    parser.showHelp(2);
  }
  if (QFileInfo::exists(arguments.at(1)))
  {
    fail("Целевая БД уже существует; импорт её не перезаписывает");
    return 1;
  }
  const auto contract = readContract(arguments.at(0));
  if (!contract || !importContract(*contract, arguments.at(1)))
  {
    return 1;
  }
  QTextStream(stdout) << "Импорт завершён: " << arguments.at(1) << Qt::endl;
  return 0;
}
