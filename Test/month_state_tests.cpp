#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdlib>
#include <vector>

#include "JournalApp.hpp"
#include "JournalLocal.hpp"
#include "SqliteConnect.hpp"

namespace
{

bool Check(bool condition, const char* expression, int line)
{
  if (!condition)
  {
    qCritical().noquote() << "CHECK failed at line" << line << ':'
                          << expression;
  }
  return condition;
}

#define TEST_CHECK(expression)                                                 \
  do                                                                           \
  {                                                                            \
    if (!Check((expression), #expression, __LINE__))                           \
    {                                                                          \
      return false;                                                            \
    }                                                                          \
  } while (false)

class TemporaryDatabase
{
public:
  TemporaryDatabase()
      : path_(directory_.filePath("journal.db")),
        opened_(directory_.isValid() && storage_.open(path_))
  {
  }

  bool isOpen() const
  {
    return opened_;
  }
  const QString& path() const
  {
    return path_;
  }
  SqliteConnect& storage()
  {
    return storage_;
  }

private:
  QTemporaryDir directory_;
  QString path_;
  SqliteConnect storage_;
  bool opened_;
};

Participant MakeParticipant(const QString& name)
{
  return {{QUuid::createUuid().toString(QUuid::WithoutBraces)}, name};
}

bool ExecuteSql(const QString& path, const QString& sql)
{
  const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool ok = false;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(path);
    if (db.open())
    {
      QSqlQuery query(db);
      ok = query.exec(sql);
    }
  }
  QSqlDatabase::removeDatabase(connection);
  return ok;
}

bool FreshMonthIsMissing()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  const MonthStateResult state = db.storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Missing);
  TEST_CHECK(state.errorMessage.isEmpty());
  return true;
}

bool ConfiguredEmptyMonthIsReady()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  TEST_CHECK(db.storage().saveActiveDays(2026, 7, {1, 3, 5}));
  TEST_CHECK(db.storage().getParticipantsForMonth(2026, 7).empty());
  TEST_CHECK(db.storage().getMonthState(2026, 7).state == MonthState::Ready);
  return true;
}

bool RemovingLastParticipantKeepsMonthReady()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  const Participant participant = MakeParticipant("Alice");
  TEST_CHECK(db.storage().addParticipantToMonth(2026, 7, participant));
  TEST_CHECK(db.storage().removeParticipantFromMonth(2026, 7, participant.id));
  TEST_CHECK(db.storage().getParticipantsForMonth(2026, 7).empty());
  TEST_CHECK(db.storage().getMonthState(2026, 7).state == MonthState::Ready);
  return true;
}

bool DisabledDayAttendanceIsPreserved()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  const Participant participant = MakeParticipant("Alice");
  TEST_CHECK(db.storage().saveActiveDays(2026, 7, {1, 2}));
  TEST_CHECK(db.storage().addParticipantToMonth(2026, 7, participant));
  TEST_CHECK(db.storage().saveAttendance(2026, 7, {{participant.id, 2, true}}));
  TEST_CHECK(db.storage().saveActiveDays(2026, 7, {1}));

  const auto records = db.storage().getMonth(2026, 7);
  bool preserved = false;
  for (const AttendanceRecord& record : records)
  {
    preserved = preserved || (record.participantId == participant.id &&
                              record.day == 2 && record.isChecked);
  }
  TEST_CHECK(preserved);
  return true;
}

bool AttendanceCountUsesOnlyActiveCheckedDays()
{
  const QHash<int, bool> attendanceByDay{
      {1, true}, {2, true}, {3, false}, {5, true}};
  TEST_CHECK(CountCheckedActiveDays({1, 3, 5}, attendanceByDay) == 2);
  TEST_CHECK(CountCheckedActiveDays({3}, attendanceByDay) == 0);
  TEST_CHECK(CountCheckedActiveDays({}, attendanceByDay) == 0);
  return true;
}

bool FailedReplaceDoesNotModifyMonth()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  const Participant participant = MakeParticipant("Alice");
  TEST_CHECK(db.storage().saveActiveDays(2026, 7, {1}));
  TEST_CHECK(db.storage().addParticipantToMonth(2026, 7, participant));

  MonthSnapshot invalid;
  invalid.activeDays = {1};
  invalid.attendance = {{participant.id, 1, false}};
  TEST_CHECK(!db.storage().replaceMonth(2026, 7, invalid));
  const auto participants = db.storage().getParticipantsForMonth(2026, 7);
  TEST_CHECK(participants.size() == 1);
  TEST_CHECK(participants.front().id == participant.id);
  return true;
}

bool StorageErrorIsNotReportedAsMissing()
{
  TemporaryDatabase db;
  TEST_CHECK(db.isOpen());
  TEST_CHECK(ExecuteSql(db.path(), "DROP TABLE month_days"));
  const MonthStateResult state = db.storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Error);
  TEST_CHECK(!state.errorMessage.isEmpty());
  return true;
}

bool CopyFromEmptySourceCreatesTargetMonth()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  auto sqlite = std::make_unique<SqliteConnect>();
  TEST_CHECK(sqlite->open(directory.filePath("journal.db")));
  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  JournalApp app(std::move(local));

  const CopyUsersResult result = app.copyUsersFromMonth(
      2026, 6, 2026, 7, CopyScheduleMode::ApplySourceWeekdays);
  TEST_CHECK(result.ok);
  TEST_CHECK(result.copied == 0);
  TEST_CHECK(app.getMonthState(2026, 7).state == MonthState::Ready);
  TEST_CHECK(app.loadMonth(2026, 7).activeDays.size() == 31);
  return true;
}

bool CopyWeekdayPatternMapsToTargetDates()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  auto sqlite = std::make_unique<SqliteConnect>();
  TEST_CHECK(sqlite->open(directory.filePath("journal.db")));

  const Participant participant = MakeParticipant("Alice");
  TEST_CHECK(sqlite->saveActiveDays(2026, 6, {1, 2}));
  TEST_CHECK(sqlite->addParticipantToMonth(2026, 6, participant));

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  JournalApp app(std::move(local));
  const CopyUsersResult result =
      app.copyUsersFromMonth(2026, 6, 2026, 7,
                             CopyScheduleMode::ApplySourceWeekdays);
  TEST_CHECK(result.ok);
  TEST_CHECK(result.copied == 1);

  const MonthSnapshot target = app.loadMonth(2026, 7);
  const QVector<int> expectedDays{6, 7, 13, 14, 20, 21, 27, 28};
  TEST_CHECK(target.state == MonthState::Ready);
  TEST_CHECK(target.activeDays == expectedDays);
  TEST_CHECK(target.participants.size() == 1);
  TEST_CHECK(target.participants.front().id == participant.id);
  TEST_CHECK(target.attendance.size() ==
             static_cast<std::size_t>(expectedDays.size()));
  for (const AttendanceRecord& record : target.attendance)
  {
    TEST_CHECK(expectedDays.contains(record.day));
    TEST_CHECK(!record.isChecked);
  }
  return true;
}

bool CopyWithoutScheduleKeepsTargetDates()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  auto sqlite = std::make_unique<SqliteConnect>();
  TEST_CHECK(sqlite->open(directory.filePath("journal.db")));

  const Participant participant = MakeParticipant("Alice");
  TEST_CHECK(sqlite->saveActiveDays(2026, 6, {1, 2}));
  TEST_CHECK(sqlite->addParticipantToMonth(2026, 6, participant));
  TEST_CHECK(sqlite->saveActiveDays(2026, 7, {3, 10}));

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  JournalApp app(std::move(local));
  const CopyUsersResult result = app.copyUsersFromMonth(
      2026, 6, 2026, 7, CopyScheduleMode::KeepTargetDates);
  TEST_CHECK(result.ok);
  TEST_CHECK(result.copied == 1);

  const MonthSnapshot target = app.loadMonth(2026, 7);
  TEST_CHECK(target.activeDays == QVector<int>({3, 10}));
  TEST_CHECK(target.attendance.size() == 2);
  for (const AttendanceRecord& record : target.attendance)
  {
    TEST_CHECK(record.day == 3 || record.day == 10);
  }
  return true;
}

bool LegacyDatesMigrateToProfileSchema()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString path = directory.filePath("legacy.db");
  const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const int configuredYear = QDate::currentDate().year() - 1;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(path);
    TEST_CHECK(db.open());
    QSqlQuery query(db);
    TEST_CHECK(query.exec(
        "CREATE TABLE users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT "
        "NOT NULL, date TEXT NOT NULL, is_checked INTEGER NOT NULL)"));
    TEST_CHECK(query.exec(
        "CREATE TABLE month_days(year INTEGER NOT NULL, month INTEGER NOT "
        "NULL, day INTEGER NOT NULL, PRIMARY KEY(year, month, day))"));
    TEST_CHECK(query.exec(
        "INSERT INTO users(name, date, is_checked) VALUES('Alice','01.07',1)"));
    query.prepare("INSERT INTO month_days(year, month, day) VALUES(:year,7,1)");
    query.bindValue(":year", configuredYear);
    TEST_CHECK(query.exec());
  }
  QSqlDatabase::removeDatabase(connection);

  SqliteConnect storage;
  TEST_CHECK(storage.open(path));
  TEST_CHECK(storage.getMonthState(configuredYear, 7).state ==
             MonthState::Ready);
  const auto participants = storage.getParticipantsForMonth(configuredYear, 7);
  TEST_CHECK(participants.size() == 1);
  TEST_CHECK(participants.front().displayName == "Alice");
  const auto records = storage.getMonth(configuredYear, 7);
  TEST_CHECK(records.size() == 1);
  TEST_CHECK(records.front().isChecked);
  TEST_CHECK(storage.getMonthState(configuredYear + 1, 7).state ==
             MonthState::Missing);
  return true;
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  const struct
  {
    const char* name;
    bool (*run)();
  } tests[] = {
      {"fresh month is missing", FreshMonthIsMissing},
      {"configured empty month is ready", ConfiguredEmptyMonthIsReady},
      {"removing last participant keeps month ready",
       RemovingLastParticipantKeepsMonthReady},
      {"disabled-day attendance is preserved",
       DisabledDayAttendanceIsPreserved},
      {"attendance count uses active checked days",
       AttendanceCountUsesOnlyActiveCheckedDays},
      {"failed replace does not modify month", FailedReplaceDoesNotModifyMonth},
      {"storage error is not missing", StorageErrorIsNotReportedAsMissing},
      {"copy from empty source creates target",
       CopyFromEmptySourceCreatesTargetMonth},
      {"copy weekday pattern maps to target dates",
       CopyWeekdayPatternMapsToTargetDates},
      {"copy without schedule keeps target dates",
       CopyWithoutScheduleKeepsTargetDates},
      {"legacy dates migrate to profile schema",
       LegacyDatesMigrateToProfileSchema}};

  int testIndex = 0;
  for (const auto& test : tests)
  {
    if (!test.run())
    {
      qCritical() << "FAILED:" << test.name;
      return testIndex + 1;
    }
    qInfo() << "PASSED:" << test.name;
    ++testIndex;
  }
  return EXIT_SUCCESS;
}
