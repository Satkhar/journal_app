#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace
{

constexpr auto kV1ParticipantId =
    "11111111-1111-4111-8111-111111111111";
constexpr auto kV2ParticipantId =
    "22222222-2222-4222-8222-222222222222";

struct ProcessResult
{
  bool started = false;
  bool finished = false;
  QProcess::ExitStatus exitStatus = QProcess::CrashExit;
  int exitCode = -1;
  QByteArray standardOutput;
  QByteArray standardError;
};

bool check(bool condition, const QString& message)
{
  if (!condition)
  {
    qCritical().noquote() << message;
  }
  return condition;
}

bool writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    return check(false,
                 QString("Не удалось записать %1: %2")
                     .arg(path, file.errorString()));
  }
  return check(file.write(contents) == contents.size(),
               QString("Файл записан не полностью: %1").arg(path));
}

ProcessResult runImporter(const QString& importerPath,
                          const QString& jsonPath,
                          const QString& databasePath)
{
  QProcess process;
  process.start(importerPath, {jsonPath, databasePath});

  ProcessResult result;
  result.started = process.waitForStarted(5000);
  if (!result.started)
  {
    result.standardError = process.errorString().toUtf8();
    return result;
  }
  result.finished = process.waitForFinished(30000);
  if (!result.finished)
  {
    process.kill();
    process.waitForFinished(5000);
  }
  result.exitStatus = process.exitStatus();
  result.exitCode = process.exitCode();
  result.standardOutput = process.readAllStandardOutput();
  result.standardError = process.readAllStandardError();
  return result;
}

bool processSucceeded(const ProcessResult& result, const QString& scenario)
{
  return check(result.started && result.finished &&
                   result.exitStatus == QProcess::NormalExit &&
                   result.exitCode == 0,
               QString("%1: импорт завершился ошибкой: %2")
                   .arg(scenario,
                        QString::fromUtf8(result.standardError)));
}

bool processFailed(const ProcessResult& result, const QString& scenario)
{
  return check(result.started && result.finished &&
                   result.exitStatus == QProcess::NormalExit &&
                   result.exitCode != 0,
               QString("%1: ожидалась управляемая ошибка процесса")
                   .arg(scenario));
}

bool expectRows(QSqlDatabase& database, const QString& sql,
                const QList<QStringList>& expected, const QString& label)
{
  QSqlQuery query(database);
  if (!query.exec(sql))
  {
    return check(false,
                 QString("%1: SQL-ошибка: %2")
                     .arg(label, query.lastError().text()));
  }
  for (int row = 0; row < expected.size(); ++row)
  {
    if (!query.next())
    {
      return check(false,
                   QString("%1: отсутствует строка %2").arg(label).arg(row));
    }
    for (int column = 0; column < expected.at(row).size(); ++column)
    {
      const QString actual = query.value(column).toString();
      if (actual != expected.at(row).at(column))
      {
        return check(
            false,
            QString("%1: строка %2, столбец %3: '%4' вместо '%5'")
                .arg(label)
                .arg(row)
                .arg(column)
                .arg(actual, expected.at(row).at(column)));
      }
    }
  }
  return check(!query.next(), QString("%1: найдены лишние строки").arg(label));
}

template <typename Verify>
bool verifyDatabase(const QString& path, Verify verify)
{
  const QString connectionName =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool verified = false;
  {
    QSqlDatabase database =
        QSqlDatabase::addDatabase("QSQLITE", connectionName);
    database.setDatabaseName(path);
    if (!database.open())
    {
      check(false,
            QString("Не удалось открыть тестовую БД: %1")
                .arg(database.lastError().text()));
    }
    else
    {
      verified = expectRows(database, "PRAGMA integrity_check", {{"ok"}},
                            "integrity_check") &&
                 expectRows(database, "PRAGMA foreign_key_check", {},
                            "foreign_key_check") &&
                 verify(database);
      database.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return verified;
}

QByteArray validV1Contract()
{
  return R"json({
  "format_version": 1,
  "year": 2025,
  "month": 7,
  "active_days": [3, 10],
  "participants": [
    {
      "id": "11111111-1111-4111-8111-111111111111",
      "historical_name": "Falcon",
      "full_name": "Test One",
      "contact": "test-v1@example.invalid",
      "rank": "squire",
      "combat_hand": "left",
      "notes": "v1 note"
    }
  ],
  "attendance": [
    {
      "participant_id": "11111111-1111-4111-8111-111111111111",
      "day": 10
    }
  ],
  "day_markers": [
    {
      "participant_id": "11111111-1111-4111-8111-111111111111",
      "day": 3,
      "kind_mask": 9,
      "note": "payment note"
    }
  ]
})json";
}

QByteArray validV2Contract()
{
  return R"json({
  "format_version": 2,
  "participants": [
    {
      "id": "22222222-2222-4222-8222-222222222222",
      "historical_name": "Aldric",
      "full_name": "Test Two",
      "contact": "test-v2@example.invalid",
      "birthday": {"day": 8, "month": 4, "year": 1990},
      "rank": "knight",
      "combat_hand": "right",
      "training_start_month": {"year": 2020, "month": 9},
      "joined_club_on": "2021-01-02",
      "notes": "v2 note",
      "rank_history": [
        {"rank": "squire", "obtained_on": null},
        {"rank": "knight", "obtained_on": "2024-05-06"}
      ]
    }
  ],
  "months": [
    {
      "year": 2026,
      "month": 2,
      "active_days": [2, 9],
      "participant_ids": [
        "22222222-2222-4222-8222-222222222222"
      ],
      "attendance": [
        {
          "participant_id": "22222222-2222-4222-8222-222222222222",
          "day": 9
        }
      ],
      "day_markers": [
        {
          "participant_id": "22222222-2222-4222-8222-222222222222",
          "day": 2,
          "kind_mask": 18,
          "note": "iron training"
        }
      ]
    }
  ]
})json";
}

QByteArray duplicateProfileContract()
{
  return R"json({
  "format_version": 2,
  "participants": [
    {
      "id": "22222222-2222-4222-8222-222222222222",
      "historical_name": "First",
      "full_name": "",
      "contact": "",
      "birthday": null,
      "rank": "guest",
      "combat_hand": "unknown",
      "training_start_month": null,
      "joined_club_on": null,
      "notes": "",
      "rank_history": []
    },
    {
      "id": "22222222-2222-4222-8222-222222222222",
      "historical_name": "Second",
      "full_name": "",
      "contact": "",
      "birthday": null,
      "rank": "guest",
      "combat_hand": "unknown",
      "training_start_month": null,
      "joined_club_on": null,
      "notes": "",
      "rank_history": []
    }
  ],
  "months": [
    {
      "year": 2026,
      "month": 3,
      "active_days": [1],
      "participant_ids": [
        "22222222-2222-4222-8222-222222222222"
      ],
      "attendance": [],
      "day_markers": []
    }
  ]
})json";
}

QByteArray invalidV1MarkerContract()
{
  QByteArray contract = validV1Contract();
  contract.replace("\"kind_mask\": 9", "\"kind_mask\": 64");
  return contract;
}

bool happyV1(const QString& importerPath, const QString& directory)
{
  const QString jsonPath = directory + "/happy-v1.json";
  const QString databasePath = directory + "/happy-v1.db";
  if (!writeFile(jsonPath, validV1Contract()))
  {
    return false;
  }
  if (!processSucceeded(runImporter(importerPath, jsonPath, databasePath),
                        "happy v1"))
  {
    return false;
  }
  return verifyDatabase(
      databasePath,
      [](QSqlDatabase& database)
      {
        return expectRows(
                   database,
                   QString("SELECT display_name, historical_name, full_name, "
                           "contact, rank, combat_hand, notes FROM "
                           "participants WHERE id = '%1'")
                       .arg(kV1ParticipantId),
                   {{"Falcon", "Falcon", "Test One",
                     "test-v1@example.invalid", "squire", "left",
                     "v1 note"}},
                   "v1 profile") &&
               expectRows(database,
                          "SELECT day FROM month_days WHERE year = 2025 AND "
                          "month = 7 ORDER BY day",
                          {{"3"}, {"10"}}, "v1 days") &&
               expectRows(
                   database,
                   QString("SELECT day, is_checked FROM attendance WHERE "
                           "year = 2025 AND month = 7 AND "
                           "participant_id = '%1' ORDER BY day")
                       .arg(kV1ParticipantId),
                   {{"3", "0"}, {"10", "1"}}, "v1 attendance") &&
               expectRows(
                   database,
                   QString("SELECT day, kind_mask, note FROM "
                           "participant_day_markers WHERE year = 2025 AND "
                           "month = 7 AND participant_id = '%1'")
                       .arg(kV1ParticipantId),
                   {{"3", "9", "payment note"}}, "v1 marker");
      });
}

bool happyV2(const QString& importerPath, const QString& directory)
{
  const QString jsonPath = directory + "/happy-v2.json";
  const QString databasePath = directory + "/happy-v2.db";
  if (!writeFile(jsonPath, validV2Contract()))
  {
    return false;
  }
  if (!processSucceeded(runImporter(importerPath, jsonPath, databasePath),
                        "happy v2"))
  {
    return false;
  }
  return verifyDatabase(
      databasePath,
      [](QSqlDatabase& database)
      {
        return expectRows(
                   database,
                   QString("SELECT display_name, historical_name, full_name, "
                           "contact, birth_day, birth_month, birth_year, rank, "
                           "combat_hand, training_start_year, "
                           "training_start_month, club_joined_on, notes FROM "
                           "participants WHERE id = '%1'")
                       .arg(kV2ParticipantId),
                   {{"Aldric", "Aldric", "Test Two",
                     "test-v2@example.invalid", "8", "4", "1990",
                     "knight", "right", "2020", "9", "2021-01-02",
                     "v2 note"}},
                   "v2 profile") &&
               expectRows(
                   database,
                   QString("SELECT rank, COALESCE(obtained_on, '<null>') FROM "
                           "participant_rank_history WHERE "
                           "participant_id = '%1' ORDER BY rank")
                       .arg(kV2ParticipantId),
                   {{"knight", "2024-05-06"}, {"squire", "<null>"}},
                   "v2 rank history") &&
               expectRows(
                   database,
                   QString("SELECT day, is_checked FROM attendance WHERE "
                           "year = 2026 AND month = 2 AND "
                           "participant_id = '%1' ORDER BY day")
                       .arg(kV2ParticipantId),
                   {{"2", "0"}, {"9", "1"}}, "v2 attendance") &&
               expectRows(
                   database,
                   QString("SELECT day, kind_mask, note FROM "
                           "participant_day_markers WHERE year = 2026 AND "
                           "month = 2 AND participant_id = '%1'")
                       .arg(kV2ParticipantId),
                   {{"2", "18", "iron training"}}, "v2 marker");
      });
}

bool duplicateProfileFails(const QString& importerPath,
                           const QString& directory)
{
  const QString jsonPath = directory + "/duplicate-profile.json";
  const QString databasePath = directory + "/duplicate-profile.db";
  if (!writeFile(jsonPath, duplicateProfileContract()))
  {
    return false;
  }
  const ProcessResult result =
      runImporter(importerPath, jsonPath, databasePath);
  return processFailed(result, "duplicate profile") &&
         check(!QFile::exists(databasePath),
               "Дубликат UUID оставил целевую БД");
}

bool invalidMarkerCleansTemporaryDatabase(const QString& importerPath,
                                          const QString& directory)
{
  const QString jsonPath = directory + "/invalid-marker.json";
  const QString databasePath = directory + "/invalid-marker.db";
  if (!writeFile(jsonPath, invalidV1MarkerContract()))
  {
    return false;
  }
  const ProcessResult result =
      runImporter(importerPath, jsonPath, databasePath);
  const QStringList leftovers =
      QDir(directory).entryList({"invalid-marker.db.importing-*"},
                                QDir::Files | QDir::Hidden | QDir::System);
  return processFailed(result, "invalid marker") &&
         check(!QFile::exists(databasePath),
               "Ошибка импорта оставила целевую БД") &&
         check(leftovers.isEmpty(),
               "Ошибка импорта оставила временные файлы БД");
}

bool existingTargetIsPreserved(const QString& importerPath,
                               const QString& directory)
{
  const QString jsonPath = directory + "/existing-target.json";
  const QString databasePath = directory + "/existing-target.db";
  const QByteArray sentinel = "existing database sentinel";
  if (!writeFile(jsonPath, validV1Contract()) ||
      !writeFile(databasePath, sentinel))
  {
    return false;
  }
  const ProcessResult result =
      runImporter(importerPath, jsonPath, databasePath);
  QFile target(databasePath);
  const bool opened = target.open(QIODevice::ReadOnly);
  const QByteArray actual = opened ? target.readAll() : QByteArray();
  return processFailed(result, "existing target") &&
         check(opened, "Существующий целевой файл исчез") &&
         check(actual == sentinel,
               "Импортер изменил существующий целевой файл");
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  if (argc != 2)
  {
    qCritical() << "Ожидался путь к journal_legacy_importer";
    return 2;
  }
  const QString importerPath = QString::fromLocal8Bit(argv[1]);
  QTemporaryDir temporaryDirectory;
  if (!check(temporaryDirectory.isValid(),
             "Не удалось создать временный каталог"))
  {
    return 1;
  }

  bool passed = true;
  passed &= happyV1(importerPath, temporaryDirectory.path());
  passed &= happyV2(importerPath, temporaryDirectory.path());
  passed &= duplicateProfileFails(importerPath, temporaryDirectory.path());
  passed &= invalidMarkerCleansTemporaryDatabase(
      importerPath, temporaryDirectory.path());
  passed &= existingTargetIsPreserved(importerPath,
                                      temporaryDirectory.path());
  return passed ? 0 : 1;
}
