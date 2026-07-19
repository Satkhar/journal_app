#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "SqliteConnect.hpp"

namespace
{

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

bool importContract(const QJsonObject& root, const QString& databasePath)
{
  if (root.value("format_version").toInt() != 1 ||
      !root.value("active_days").isArray() ||
      !root.value("participants").isArray() ||
      !root.value("attendance").isArray() ||
      !root.value("day_markers").isArray())
  {
    return fail("Неподдерживаемый или неполный JSON-контракт");
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
      return fail("JSON содержит неизвестное звание или боевую руку");
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
                          object.value("day").toInt(),
                          true});
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
      return fail("JSON содержит неизвестную комбинацию отметок");
    }
    const ParticipantDayMarker marker{
        {object.value("participant_id").toString()},
        object.value("day").toInt(),
        *kinds,
        object.value("note").toString()};
    if (!storage.saveDayMarker(year, month, marker))
    {
      return fail(storage.lastError());
    }
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
    QFile::remove(arguments.at(1));
    return 1;
  }
  QTextStream(stdout) << "Импорт завершён: " << arguments.at(1) << Qt::endl;
  return 0;
}
