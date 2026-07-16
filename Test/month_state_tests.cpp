#include <QApplication>
#include <QCalendarWidget>
#include <QDate>
#include <QDir>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <array>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "JournalApp.hpp"
#include "SqliteConnect.hpp"
#include "mainwindow.hpp"

namespace
{

bool Check(bool condition, const char* expression, int line)
{
  if (condition)
  {
    return true;
  }

  qCritical().noquote() << "CHECK failed at line" << line << ":" << expression;
  return false;
}

#define TEST_CHECK(expression)                                                \
  do                                                                          \
  {                                                                           \
    if (!Check((expression), #expression, __LINE__))                           \
    {                                                                         \
      return false;                                                           \
    }                                                                         \
  } while (false)

class TemporaryDatabase
{
 public:
  TemporaryDatabase()
      : path_(directory_.filePath("journal.db")),
        opened_(directory_.isValid() && sqlite_.open(path_))
  {
  }

  bool IsOpen() const
  {
    return opened_;
  }

  SqliteConnect& Storage()
  {
    return sqlite_;
  }

  const QString& Path() const
  {
    return path_;
  }

 private:
  QTemporaryDir directory_;
  QString path_;
  SqliteConnect sqlite_;
  bool opened_;
};

bool ExecuteSql(const QString& path, const QString& sql)
{
  const QString connectionName =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool success = false;
  {
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    database.setDatabaseName(path);
    if (database.open())
    {
      QSqlQuery query(database);
      success = query.exec(sql);
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return success;
}

int QueryInt(const QString& path, const QString& sql, bool* ok)
{
  const QString connectionName =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  int value = 0;
  bool success = false;
  {
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    database.setDatabaseName(path);
    if (database.open())
    {
      QSqlQuery query(database);
      success = query.exec(sql) && query.next();
      if (success)
      {
        value = query.value(0).toInt();
      }
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  if (ok != nullptr)
  {
    *ok = success;
  }
  return value;
}

bool FreshDatabaseMonthIsMissing()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  const MonthStateResult state = database.Storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Missing);
  TEST_CHECK(state.errorMessage.isEmpty());
  return true;
}

bool SaveActiveDaysMarksMonthReadyWithoutUsers()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  TEST_CHECK(database.Storage().saveActiveDays(2026, 7, {1, 3, 5}));
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7).isEmpty());

  const MonthStateResult state = database.Storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Ready);
  TEST_CHECK(state.errorMessage.isEmpty());
  return true;
}

bool DeleteLastUserKeepsMonthReady()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  TEST_CHECK(database.Storage().addUser(2026, 7, "Alice"));
  TEST_CHECK(!database.Storage().getUsersForMonth(2026, 7).isEmpty());
  TEST_CHECK(database.Storage().deleteUser(2026, 7, "Alice"));
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7).isEmpty());

  const MonthStateResult state = database.Storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Ready);
  TEST_CHECK(state.errorMessage.isEmpty());
  return true;
}

bool DifferentYearAndMonthStayMissing()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  TEST_CHECK(database.Storage().saveActiveDays(2026, 7, {2, 4, 6}));
  TEST_CHECK(database.Storage().addUser(2026, 7, "Alice"));
  TEST_CHECK(database.Storage().getMonthState(2026, 7).state ==
             MonthState::Ready);
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7).contains("Alice"));
  TEST_CHECK(database.Storage().getMonthState(2027, 7).state ==
             MonthState::Missing);
  TEST_CHECK(database.Storage().getUsersForMonth(2027, 7).isEmpty());
  TEST_CHECK(database.Storage().getMonthState(2026, 8).state ==
             MonthState::Missing);

  TEST_CHECK(database.Storage().saveActiveDays(2026, 7, {1, 2}));
  TEST_CHECK(database.Storage().getMonth(2026, 7).size() == 2);
  TEST_CHECK(database.Storage().saveActiveDays(2027, 7, {1, 3}));
  TEST_CHECK(database.Storage().addUser(2027, 7, "Bob"));
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7) ==
             QStringList{"Alice"});
  TEST_CHECK(database.Storage().getUsersForMonth(2027, 7) ==
             QStringList{"Bob"});
  TEST_CHECK(database.Storage().getMonth(2027, 7).size() == 2);
  return true;
}

bool LegacyDatesAreBoundToOneYear()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString path = directory.filePath("legacy.db");
  const QString connectionName =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  const int configuredYear = QDate::currentDate().year() - 1;

  {
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    database.setDatabaseName(path);
    TEST_CHECK(database.open());
    QSqlQuery query(database);
    TEST_CHECK(query.exec(
        "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, date TEXT NOT NULL, is_checked INTEGER NOT NULL)"));
    TEST_CHECK(query.exec(
        "CREATE TABLE month_days (year INTEGER NOT NULL, month INTEGER NOT NULL, "
        "day INTEGER NOT NULL, PRIMARY KEY(year, month, day))"));
    TEST_CHECK(query.exec(
        "INSERT INTO users(name, date, is_checked) VALUES('Alice', '01.07', 0)"));
    query.prepare(
        "INSERT INTO month_days(year, month, day) VALUES(:year, 7, 1)");
    query.bindValue(":year", configuredYear);
    TEST_CHECK(query.exec());
    database.close();
  }
  QSqlDatabase::removeDatabase(connectionName);

  SqliteConnect storage;
  TEST_CHECK(storage.open(path));
  TEST_CHECK(storage.getMonthState(configuredYear, 7).state ==
             MonthState::Ready);
  TEST_CHECK(storage.getUsersForMonth(configuredYear, 7).contains("Alice"));
  TEST_CHECK(storage.getMonthState(configuredYear + 1, 7).state ==
             MonthState::Missing);
  TEST_CHECK(storage.getUsersForMonth(configuredYear + 1, 7).isEmpty());
  return true;
}

bool StorageFailureIsNotReportedAsMissing()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());
  const QString connectionName =
      QUuid::createUuid().toString(QUuid::WithoutBraces);

  {
    QSqlDatabase sabotage =
        QSqlDatabase::addDatabase("QSQLITE", connectionName);
    sabotage.setDatabaseName(database.Path());
    TEST_CHECK(sabotage.open());
    QSqlQuery query(sabotage);
    TEST_CHECK(query.exec("DROP TABLE months"));
    sabotage.close();
  }
  QSqlDatabase::removeDatabase(connectionName);

  const MonthStateResult state = database.Storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Error);
  TEST_CHECK(!state.errorMessage.isEmpty());
  return true;
}

bool SaveEmptyMonthMarksMonthReady()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  const std::vector<AttendanceRecord> records;
  TEST_CHECK(database.Storage().saveMonth(2026, 7, records));
  TEST_CHECK(database.Storage().getMonth(2026, 7).empty());

  const MonthStateResult state = database.Storage().getMonthState(2026, 7);
  TEST_CHECK(state.state == MonthState::Ready);
  TEST_CHECK(state.errorMessage.isEmpty());
  return true;
}

bool FailedMonthSetupRollsBack()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());
  TEST_CHECK(database.Storage().saveActiveDays(2026, 7, {1}));
  TEST_CHECK(database.Storage().addUser(2026, 7, "Alice"));

  const std::vector<AttendanceRecord> invalidData{{"Bob", 2, false}};
  TEST_CHECK(!database.Storage().saveMonthSetup(2026, 7, {1}, invalidData));
  TEST_CHECK(!database.Storage().lastError().isEmpty());
  TEST_CHECK(database.Storage().getActiveDays(2026, 7) == QVector<int>{1});
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7) ==
             QStringList{"Alice"});
  const std::vector<AttendanceRecord> records =
      database.Storage().getMonth(2026, 7);
  TEST_CHECK(records.size() == 1);
  TEST_CHECK(records.front().userName == "Alice");
  TEST_CHECK(records.front().day == 1);
  return true;
}

bool SuccessfulFreshMonthSetupIsComplete()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString path = directory.filePath("journal.db");

  const std::vector<AttendanceRecord> data{
      {"Alice", 1, true}, {"Alice", 3, false}};
  {
    SqliteConnect writer;
    TEST_CHECK(writer.open(path));
    TEST_CHECK(writer.saveMonthSetup(2026, 7, {3, 1, 3}, data));
  }

  SqliteConnect reader;
  TEST_CHECK(reader.open(path));
  TEST_CHECK(reader.getMonthState(2026, 7).state ==
             MonthState::Ready);
  TEST_CHECK(reader.getActiveDays(2026, 7) == QVector<int>({1, 3}));
  TEST_CHECK(reader.getUsersForMonth(2026, 7) == QStringList{"Alice"});

  const std::vector<AttendanceRecord> records =
      reader.getMonth(2026, 7);
  TEST_CHECK(records.size() == 2);
  TEST_CHECK(records[0].userName == "Alice");
  TEST_CHECK(records[0].day == 1);
  TEST_CHECK(records[0].isChecked);
  TEST_CHECK(records[1].day == 3);
  TEST_CHECK(!records[1].isChecked);
  return true;
}

bool FailedFreshMonthSetupStaysMissing()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());

  const std::vector<AttendanceRecord> invalidData{{"Alice", 2, false}};
  TEST_CHECK(!database.Storage().saveMonthSetup(2026, 7, {1}, invalidData));
  TEST_CHECK(database.Storage().getMonthState(2026, 7).state ==
             MonthState::Missing);
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7).isEmpty());
  return true;
}

bool SaveActiveDaysAbortsOnUsersReadError()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());
  TEST_CHECK(database.Storage().saveActiveDays(2026, 7, {1}));
  TEST_CHECK(ExecuteSql(database.Path(), "DROP TABLE users"));

  TEST_CHECK(!database.Storage().saveActiveDays(2026, 7, {2}));
  TEST_CHECK(!database.Storage().lastError().isEmpty());
  TEST_CHECK(database.Storage().getActiveDays(2026, 7) == QVector<int>{1});
  return true;
}

bool AddUserRollsBackOnActiveDaysReadError()
{
  TemporaryDatabase database;
  TEST_CHECK(database.IsOpen());
  TEST_CHECK(ExecuteSql(database.Path(), "DROP TABLE month_days"));

  TEST_CHECK(!database.Storage().addUser(2026, 7, "Alice"));
  TEST_CHECK(!database.Storage().lastError().isEmpty());
  TEST_CHECK(database.Storage().getUsersForMonth(2026, 7).isEmpty());

  bool queryOk = false;
  const int markerCount = QueryInt(
      database.Path(),
      "SELECT COUNT(*) FROM months WHERE year = 2026 AND month = 7",
      &queryOk);
  TEST_CHECK(queryOk);
  TEST_CHECK(markerCount == 0);
  return true;
}

class FakeStorage final : public IJournalStorage
{
 public:
  enum class ReadFailure
  {
    None,
    Users,
    ActiveDays,
    Month,
  };

  QString lastError() const override
  {
    return lastErrorValue;
  }

  MonthStateResult getMonthState(int year, int month) override
  {
    lastErrorValue.clear();
    ++getMonthStateCalls;
    if ((year == sourceYear && month == sourceMonth && !sourceUsers.isEmpty()) ||
        (year == targetYear && month == targetMonth && !targetUsers.isEmpty()))
    {
      return {MonthState::Ready, QString()};
    }
    return {MonthState::Missing, QString()};
  }

  QStringList getUsersForMonth(int year, int month) override
  {
    lastErrorValue.clear();
    if (readFailure == ReadFailure::Users)
    {
      lastErrorValue = "users read failed";
      return {};
    }
    if (year == sourceYear && month == sourceMonth)
    {
      return sourceUsers;
    }
    if (year == targetYear && month == targetMonth)
    {
      return targetUsers;
    }
    return {};
  }

  QVector<int> getActiveDays(int year, int month) override
  {
    lastErrorValue.clear();
    activeDaysReadYear = year;
    activeDaysReadMonth = month;
    if (readFailure == ReadFailure::ActiveDays)
    {
      lastErrorValue = "active days read failed";
      return {};
    }
    if (year == sourceYear && month == sourceMonth)
    {
      return sourceDays;
    }
    return targetDays;
  }

  bool saveActiveDays(int year, int month,
                      const QVector<int>& days) override
  {
    Q_UNUSED(year)
    Q_UNUSED(month)
    Q_UNUSED(days)
    ++saveActiveDaysCalls;
    return true;
  }

  std::vector<AttendanceRecord> getMonth(int year, int month) override
  {
    lastErrorValue.clear();
    if (readFailure == ReadFailure::Month)
    {
      lastErrorValue = "month read failed";
      return {};
    }
    if (year == targetYear && month == targetMonth)
    {
      return targetAttendance;
    }
    return {};
  }

  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord>& data) override
  {
    Q_UNUSED(year)
    Q_UNUSED(month)
    Q_UNUSED(data)
    ++saveMonthCalls;
    return true;
  }

  bool saveMonthSetup(int year, int month, const QVector<int>& days,
                      const std::vector<AttendanceRecord>& data) override
  {
    ++saveMonthSetupCalls;
    savedSetupYear = year;
    savedSetupMonth = month;
    savedSetupDays = days;
    savedSetupData = data;
    return true;
  }

  bool addUser(int year, int month, const QString& name) override
  {
    Q_UNUSED(year)
    Q_UNUSED(month)
    Q_UNUSED(name)
    ++addUserCalls;
    return true;
  }

  bool deleteUser(int year, int month, const QString& name) override
  {
    Q_UNUSED(year)
    Q_UNUSED(month)
    Q_UNUSED(name)
    return true;
  }

  int getMonthStateCalls{0};
  int saveActiveDaysCalls{0};
  int saveMonthCalls{0};
  int saveMonthSetupCalls{0};
  int addUserCalls{0};
  ReadFailure readFailure{ReadFailure::None};
  QString lastErrorValue;
  int sourceYear{2026};
  int sourceMonth{6};
  int targetYear{2026};
  int targetMonth{7};
  QStringList sourceUsers;
  QStringList targetUsers;
  QVector<int> sourceDays{4, 5};
  QVector<int> targetDays{1, 2, 3};
  std::vector<AttendanceRecord> targetAttendance;
  int activeDaysReadYear{0};
  int activeDaysReadMonth{0};
  int savedSetupYear{0};
  int savedSetupMonth{0};
  QVector<int> savedSetupDays;
  std::vector<AttendanceRecord> savedSetupData;
};

bool LoadMissingMonthDoesNotBootstrapStorage()
{
  auto storage = std::make_unique<FakeStorage>();
  FakeStorage* const observedStorage = storage.get();
  JournalApp app(std::move(storage));

  const MonthSnapshot snapshot = app.loadMonth(2026, 7);

  TEST_CHECK(snapshot.state == MonthState::Missing);
  TEST_CHECK(snapshot.users.isEmpty());
  TEST_CHECK(snapshot.attendance.empty());
  TEST_CHECK(observedStorage->getMonthStateCalls == 1);
  TEST_CHECK(observedStorage->addUserCalls == 0);
  TEST_CHECK(observedStorage->saveMonthCalls == 0);
  TEST_CHECK(observedStorage->saveMonthSetupCalls == 0);
  TEST_CHECK(observedStorage->saveActiveDaysCalls == 0);
  return true;
}

bool CopyUseCaseUsesSingleAtomicSetup()
{
  auto storage = std::make_unique<FakeStorage>();
  FakeStorage* const observedStorage = storage.get();
  storage->sourceUsers = {"Alice", "Bob"};
  storage->targetUsers = {"Bob"};
  storage->targetAttendance = {{"Bob", 1, true}, {"Bob", 2, false}};
  JournalApp app(std::move(storage));

  const CopyUsersResult result =
      app.copyUsersFromMonth(2026, 6, 2026, 7, false);

  TEST_CHECK(result.ok);
  TEST_CHECK(result.copied == 1);
  TEST_CHECK(result.skipped == 1);
  TEST_CHECK(observedStorage->saveMonthSetupCalls == 1);
  TEST_CHECK(observedStorage->savedSetupYear == 2026);
  TEST_CHECK(observedStorage->savedSetupMonth == 7);
  TEST_CHECK(observedStorage->savedSetupDays == QVector<int>({1, 2, 3}));
  TEST_CHECK(observedStorage->activeDaysReadYear == 2026);
  TEST_CHECK(observedStorage->activeDaysReadMonth == 7);
  TEST_CHECK(observedStorage->savedSetupData.size() == 6);
  TEST_CHECK(observedStorage->savedSetupData[0].userName == "Bob");
  TEST_CHECK(observedStorage->savedSetupData[0].day == 1);
  TEST_CHECK(observedStorage->savedSetupData[0].isChecked);
  TEST_CHECK(observedStorage->savedSetupData[3].userName == "Alice");
  TEST_CHECK(observedStorage->savedSetupData[3].day == 1);
  TEST_CHECK(!observedStorage->savedSetupData[3].isChecked);
  TEST_CHECK(observedStorage->saveActiveDaysCalls == 0);
  TEST_CHECK(observedStorage->saveMonthCalls == 0);
  TEST_CHECK(observedStorage->addUserCalls == 0);
  return true;
}

bool CopyUseCaseCanCopySourceDaysAtomically()
{
  auto storage = std::make_unique<FakeStorage>();
  FakeStorage* const observedStorage = storage.get();
  storage->sourceUsers = {"Alice"};
  storage->sourceDays = {4, 5};
  storage->targetDays = {1};
  JournalApp app(std::move(storage));

  const CopyUsersResult result =
      app.copyUsersFromMonth(2026, 6, 2026, 7, true);

  TEST_CHECK(result.ok);
  TEST_CHECK(result.copied == 1);
  TEST_CHECK(result.skipped == 0);
  TEST_CHECK(observedStorage->saveMonthSetupCalls == 1);
  TEST_CHECK(observedStorage->activeDaysReadYear == 2026);
  TEST_CHECK(observedStorage->activeDaysReadMonth == 6);
  TEST_CHECK(observedStorage->savedSetupDays == QVector<int>({4, 5}));
  TEST_CHECK(observedStorage->savedSetupData.size() == 2);
  TEST_CHECK(observedStorage->savedSetupData[0].day == 4);
  TEST_CHECK(observedStorage->savedSetupData[1].day == 5);
  return true;
}

bool EverySecondaryReadErrorInvalidatesSnapshot()
{
  const std::array<std::pair<FakeStorage::ReadFailure, QString>, 3> failures{{
      {FakeStorage::ReadFailure::Users, "users read failed"},
      {FakeStorage::ReadFailure::ActiveDays, "active days read failed"},
      {FakeStorage::ReadFailure::Month, "month read failed"},
  }};

  for (const auto& [failure, expectedError] : failures)
  {
    auto storage = std::make_unique<FakeStorage>();
    storage->readFailure = failure;
    JournalApp app(std::move(storage));

    const MonthSnapshot snapshot = app.loadMonth(2026, 7);
    TEST_CHECK(snapshot.state == MonthState::Error);
    TEST_CHECK(snapshot.errorMessage == expectedError);
  }
  return true;
}

bool FreshDatabaseShowsSetupMenuWithoutWriting()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString originalDirectory = QDir::currentPath();
  TEST_CHECK(QDir::setCurrent(directory.path()));
  const QString databasePath = directory.filePath("test_data.db");
  const bool databaseAbsentBeforeWindow = !QFileInfo::exists(databasePath);

  bool menuSeen = false;
  bool dismissClicked = false;
  bool finished = false;
  bool timedOut = false;
  bool constructionTimedOut = false;
  {
    QTimer constructionWatchdog;
    constructionWatchdog.setSingleShot(true);
    QObject::connect(&constructionWatchdog, &QTimer::timeout, [&]() {
      constructionTimedOut = true;
      if (QDialog* const dialog =
              qobject_cast<QDialog*>(QApplication::activeModalWidget()))
      {
        dialog->reject();
      }
    });
    constructionWatchdog.start(2000);
    MainWindow window;
    constructionWatchdog.stop();

    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QObject::connect(&pollTimer, &QTimer::timeout, &window, [&]() {
      QMessageBox* const menu =
          window.findChild<QMessageBox*>("monthSetupMenu");
      if (menu == nullptr)
      {
        return;
      }

      menuSeen = true;
      QPushButton* const dismissButton =
          menu->findChild<QPushButton*>("dismissMonthSetupButton");
      if (dismissButton != nullptr)
      {
        dismissButton->click();
        dismissClicked = true;
        finished = !menu->isVisible();
        eventLoop.quit();
      }
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() {
      timedOut = true;
      if (QMessageBox* const menu =
              window.findChild<QMessageBox*>("monthSetupMenu"))
      {
        menu->reject();
      }
      eventLoop.quit();
    });

    pollTimer.start();
    deadline.start(2000);
    window.show();
    eventLoop.exec();
  }

  const bool databaseExistsAfterWindow = QFileInfo::exists(databasePath);
  const bool directoryRestored = QDir::setCurrent(originalDirectory);
  TEST_CHECK(directoryRestored);
  TEST_CHECK(databaseAbsentBeforeWindow);
  TEST_CHECK(databaseExistsAfterWindow);
  TEST_CHECK(!constructionTimedOut);
  TEST_CHECK(!timedOut);
  TEST_CHECK(menuSeen);
  TEST_CHECK(dismissClicked);
  TEST_CHECK(finished);

  SqliteConnect storage;
  TEST_CHECK(storage.open(databasePath));
  const QDate today = QDate::currentDate();
  TEST_CHECK(storage.getMonthState(today.year(), today.month()).state ==
             MonthState::Missing);
  TEST_CHECK(storage.getUsersForMonth(today.year(), today.month()).isEmpty());
  return true;
}

bool CreateFromScratchButtonCreatesReadyMonth()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString originalDirectory = QDir::currentPath();
  TEST_CHECK(QDir::setCurrent(directory.path()));
  const QString databasePath = directory.filePath("test_data.db");

  bool menuSeen = false;
  bool createClicked = false;
  bool daysDialogSeen = false;
  bool daysAccepted = false;
  bool finished = false;
  bool timedOut = false;
  int phase = 0;
  {
    MainWindow window;
    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QObject::connect(&pollTimer, &QTimer::timeout, &window, [&]() {
      if (phase == 0)
      {
        QMessageBox* const menu =
            window.findChild<QMessageBox*>("monthSetupMenu");
        if (menu == nullptr)
        {
          return;
        }
        menuSeen = true;
        QPushButton* const createButton =
            menu->findChild<QPushButton*>("createMonthFromScratchButton");
        if (createButton == nullptr)
        {
          return;
        }
        phase = 1;
        createButton->click();
        createClicked = true;
        return;
      }

      if (phase == 1)
      {
        QDialog* const daysDialog =
            window.findChild<QDialog*>("monthDaysDialog");
        if (daysDialog == nullptr)
        {
          return;
        }
        daysDialogSeen = true;
        QDialogButtonBox* const buttons =
            daysDialog->findChild<QDialogButtonBox*>(
                "monthDaysDialogButtons");
        QPushButton* const okButton =
            buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        if (okButton == nullptr)
        {
          return;
        }
        phase = 2;
        okButton->click();
        daysAccepted = true;
        return;
      }

      if (phase == 2 &&
          window.findChild<QMessageBox*>("monthSetupMenu") == nullptr &&
          window.findChild<QDialog*>("monthDaysDialog") == nullptr)
      {
        finished = true;
        eventLoop.quit();
      }
    });

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() {
      timedOut = true;
      if (QDialog* const dialog =
              qobject_cast<QDialog*>(QApplication::activeModalWidget()))
      {
        dialog->reject();
      }
      eventLoop.quit();
    });

    pollTimer.start();
    deadline.start(2000);
    window.show();
    eventLoop.exec();
  }

  const bool databaseExists = QFileInfo::exists(databasePath);
  const bool directoryRestored = QDir::setCurrent(originalDirectory);
  TEST_CHECK(directoryRestored);
  TEST_CHECK(databaseExists);
  TEST_CHECK(!timedOut);
  TEST_CHECK(menuSeen);
  TEST_CHECK(createClicked);
  TEST_CHECK(daysDialogSeen);
  TEST_CHECK(daysAccepted);
  TEST_CHECK(finished);

  SqliteConnect storage;
  TEST_CHECK(storage.open(databasePath));
  const QDate today = QDate::currentDate();
  TEST_CHECK(storage.getMonthState(today.year(), today.month()).state ==
             MonthState::Ready);
  TEST_CHECK(storage.getUsersForMonth(today.year(), today.month()).isEmpty());
  TEST_CHECK(storage.getActiveDays(today.year(), today.month()).size() ==
             today.daysInMonth());
  return true;
}

bool CopyMenuActionCreatesMonthFromPreviousUsers()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString originalDirectory = QDir::currentPath();
  TEST_CHECK(QDir::setCurrent(directory.path()));
  const QString databasePath = directory.filePath("test_data.db");
  const QDate targetMonth = QDate::currentDate().addDays(
      1 - QDate::currentDate().day());
  const QDate sourceMonth = targetMonth.addMonths(-1);

  bool sourceReady = false;
  {
    SqliteConnect writer;
    const std::vector<AttendanceRecord> sourceData{
        {"Alice", 1, true}, {"Alice", 2, false}};
    sourceReady =
        writer.open(databasePath) &&
        writer.saveMonthSetup(sourceMonth.year(), sourceMonth.month(), {1, 2},
                              sourceData);
  }

  bool menuSeen = false;
  bool copyClicked = false;
  bool copyDialogSeen = false;
  bool copyAccepted = false;
  bool finished = false;
  bool timedOut = false;
  int phase = 0;
  {
    MainWindow window;
    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QObject::connect(&pollTimer, &QTimer::timeout, &window, [&]() {
      if (phase == 0)
      {
        QMessageBox* const menu =
            window.findChild<QMessageBox*>("monthSetupMenu");
        if (menu == nullptr)
        {
          return;
        }
        menuSeen = true;
        QPushButton* const copyButton =
            menu->findChild<QPushButton*>("copyMonthUsersButton");
        if (copyButton == nullptr)
        {
          return;
        }
        phase = 1;
        copyButton->click();
        copyClicked = true;
        return;
      }

      if (phase == 1)
      {
        QDialog* const copyDialog =
            window.findChild<QDialog*>("copyUsersDialog");
        if (copyDialog == nullptr)
        {
          return;
        }
        copyDialogSeen = true;
        QDialogButtonBox* const buttons =
            copyDialog->findChild<QDialogButtonBox*>("copyUsersDialogButtons");
        QPushButton* const okButton =
            buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        if (okButton == nullptr)
        {
          return;
        }
        phase = 2;
        okButton->click();
        copyAccepted = true;
        return;
      }

      if (phase == 2 &&
          window.findChild<QMessageBox*>("monthSetupMenu") == nullptr &&
          window.findChild<QDialog*>("copyUsersDialog") == nullptr)
      {
        finished = true;
        eventLoop.quit();
      }
    });

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() {
      timedOut = true;
      if (QDialog* const dialog =
              qobject_cast<QDialog*>(QApplication::activeModalWidget()))
      {
        dialog->reject();
      }
      eventLoop.quit();
    });

    pollTimer.start();
    deadline.start(2000);
    window.show();
    eventLoop.exec();
  }

  const bool directoryRestored = QDir::setCurrent(originalDirectory);
  TEST_CHECK(directoryRestored);
  TEST_CHECK(sourceReady);
  TEST_CHECK(!timedOut);
  TEST_CHECK(menuSeen);
  TEST_CHECK(copyClicked);
  TEST_CHECK(copyDialogSeen);
  TEST_CHECK(copyAccepted);
  TEST_CHECK(finished);

  SqliteConnect storage;
  TEST_CHECK(storage.open(databasePath));
  TEST_CHECK(storage.getMonthState(targetMonth.year(), targetMonth.month()).state ==
             MonthState::Ready);
  TEST_CHECK(storage.getActiveDays(targetMonth.year(), targetMonth.month()) ==
             QVector<int>({1, 2}));
  TEST_CHECK(storage.getUsersForMonth(targetMonth.year(), targetMonth.month()) ==
             QStringList{"Alice"});
  const std::vector<AttendanceRecord> targetData =
      storage.getMonth(targetMonth.year(), targetMonth.month());
  TEST_CHECK(targetData.size() == 2);
  TEST_CHECK(!targetData[0].isChecked);
  TEST_CHECK(!targetData[1].isChecked);
  return true;
}

bool MonthStateErrorBlocksWritesAndSetupMenu()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString originalDirectory = QDir::currentPath();
  TEST_CHECK(QDir::setCurrent(directory.path()));
  const QString databasePath = directory.filePath("test_data.db");

  bool sabotageSucceeded = false;
  bool baselineWritesEnabled = false;
  bool menuSeen = false;
  bool writesDisabled = false;
  bool finished = false;
  bool timedOut = false;
  {
    MainWindow window;
    QPushButton* const baselineAddButton =
        window.findChild<QPushButton*>("btnAdd");
    QPushButton* const baselineSaveButton =
        window.findChild<QPushButton*>("btnSaveCurTable");
    baselineWritesEnabled = baselineAddButton != nullptr &&
                            baselineSaveButton != nullptr &&
                            baselineAddButton->isEnabled() &&
                            baselineSaveButton->isEnabled();
    sabotageSucceeded = ExecuteSql(databasePath, "DROP TABLE months");

    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QObject::connect(&pollTimer, &QTimer::timeout, &window, [&]() {
      if (QMessageBox* const menu =
              window.findChild<QMessageBox*>("monthSetupMenu"))
      {
        menuSeen = true;
        menu->reject();
        finished = true;
        eventLoop.quit();
        return;
      }

      QPushButton* const addButton =
          window.findChild<QPushButton*>("btnAdd");
      QPushButton* const saveButton =
          window.findChild<QPushButton*>("btnSaveCurTable");
      if (addButton != nullptr && saveButton != nullptr &&
          !addButton->isEnabled() && !saveButton->isEnabled())
      {
        writesDisabled = true;
        finished = true;
        eventLoop.quit();
      }
    });

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() {
      timedOut = true;
      if (QDialog* const dialog =
              qobject_cast<QDialog*>(QApplication::activeModalWidget()))
      {
        dialog->reject();
      }
      eventLoop.quit();
    });

    pollTimer.start();
    deadline.start(2000);
    window.show();
    eventLoop.exec();
  }

  const bool directoryRestored = QDir::setCurrent(originalDirectory);
  TEST_CHECK(directoryRestored);
  TEST_CHECK(sabotageSucceeded);
  TEST_CHECK(baselineWritesEnabled);
  TEST_CHECK(!timedOut);
  TEST_CHECK(finished);
  TEST_CHECK(!menuSeen);
  TEST_CHECK(writesDisabled);
  return true;
}

bool CalendarTransitionShowsOnlyCurrentMissingMonth()
{
  QTemporaryDir directory;
  TEST_CHECK(directory.isValid());
  const QString originalDirectory = QDir::currentPath();
  TEST_CHECK(QDir::setCurrent(directory.path()));
  const QString databasePath = directory.filePath("test_data.db");
  const QDate currentMonth = QDate::currentDate().addDays(
      1 - QDate::currentDate().day());
  const QDate staleTarget = currentMonth.addMonths(1);
  const QDate expectedTarget = currentMonth.addMonths(2);

  bool currentMonthInitialized = false;
  {
    SqliteConnect writer;
    currentMonthInitialized =
        writer.open(databasePath) &&
        writer.saveActiveDays(currentMonth.year(), currentMonth.month(), {1});
  }

  int menuCount = 0;
  bool wrongTargetSeen = false;
  bool finished = false;
  bool timedOut = false;
  bool calendarFound = false;
  {
    MainWindow window;
    QCalendarWidget* const calendar =
        window.findChild<QCalendarWidget*>("calendarWidget");
    calendarFound = calendar != nullptr;
    if (calendar != nullptr)
    {
      // Оба callbacks уже queued. Первый обязан стать stale по request id.
      calendar->setCurrentPage(staleTarget.year(), staleTarget.month());
      calendar->setCurrentPage(expectedTarget.year(), expectedTarget.month());
    }

    QEventLoop eventLoop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QObject::connect(&pollTimer, &QTimer::timeout, &window, [&]() {
      QMessageBox* const menu =
          window.findChild<QMessageBox*>("monthSetupMenu");
      if (menu == nullptr)
      {
        return;
      }

      const QString expectedLabel =
          QString("%1.%2")
              .arg(expectedTarget.month(), 2, 10, QLatin1Char('0'))
              .arg(expectedTarget.year());
      wrongTargetSeen = wrongTargetSeen || !menu->text().contains(expectedLabel);

      QPushButton* const dismissButton =
          menu->findChild<QPushButton*>("dismissMonthSetupButton");
      if (dismissButton == nullptr)
      {
        return;
      }

      ++menuCount;
      dismissButton->click();
      if (menuCount == 1 && calendar != nullptr)
      {
        // Уход и возврат должны снять session-dismiss для месяца.
        QTimer::singleShot(0, &window, [calendar, staleTarget, expectedTarget]() {
          calendar->setCurrentPage(staleTarget.year(), staleTarget.month());
          calendar->setCurrentPage(expectedTarget.year(),
                                   expectedTarget.month());
        });
        return;
      }

      if (menuCount == 2)
      {
        finished = true;
        eventLoop.quit();
      }
    });

    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() {
      timedOut = true;
      if (QMessageBox* const menu =
              window.findChild<QMessageBox*>("monthSetupMenu"))
      {
        menu->reject();
      }
      eventLoop.quit();
    });

    pollTimer.start();
    deadline.start(2000);
    window.show();
    eventLoop.exec();
  }

  const bool directoryRestored = QDir::setCurrent(originalDirectory);
  TEST_CHECK(directoryRestored);
  TEST_CHECK(currentMonthInitialized);
  TEST_CHECK(calendarFound);
  TEST_CHECK(!timedOut);
  TEST_CHECK(finished);
  TEST_CHECK(menuCount == 2);
  TEST_CHECK(!wrongTargetSeen);

  SqliteConnect storage;
  TEST_CHECK(storage.open(databasePath));
  TEST_CHECK(storage.getMonthState(expectedTarget.year(),
                                   expectedTarget.month()).state ==
             MonthState::Missing);
  return true;
}

struct TestCase
{
  const char* name;
  bool (*run)();
};

}  // namespace

int main(int argc, char* argv[])
{
  QApplication application(argc, argv);
  Q_UNUSED(application)
  qputenv("JOURNAL_STORAGE_MODE", "local");

  const std::array<TestCase, 21> tests{{
      {"fresh database month is Missing", FreshDatabaseMonthIsMissing},
      {"saveActiveDays marks Ready without users",
       SaveActiveDaysMarksMonthReadyWithoutUsers},
      {"delete last user keeps Ready", DeleteLastUserKeepsMonthReady},
      {"different year and month stay Missing",
       DifferentYearAndMonthStayMissing},
      {"legacy dates are bound to one year", LegacyDatesAreBoundToOneYear},
      {"storage failure is not Missing", StorageFailureIsNotReportedAsMissing},
      {"saveMonth(empty) marks Ready", SaveEmptyMonthMarksMonthReady},
      {"failed month setup rolls back", FailedMonthSetupRollsBack},
      {"successful fresh month setup is complete",
       SuccessfulFreshMonthSetupIsComplete},
      {"failed fresh month setup stays Missing",
       FailedFreshMonthSetupStaysMissing},
      {"saveActiveDays aborts on users read error",
       SaveActiveDaysAbortsOnUsersReadError},
      {"addUser rolls back on active days read error",
       AddUserRollsBackOnActiveDaysReadError},
      {"loadMonth does not bootstrap storage",
       LoadMissingMonthDoesNotBootstrapStorage},
      {"copy use case uses one atomic setup", CopyUseCaseUsesSingleAtomicSetup},
      {"copy use case can copy source days atomically",
       CopyUseCaseCanCopySourceDaysAtomically},
      {"every secondary read error invalidates snapshot",
       EverySecondaryReadErrorInvalidatesSnapshot},
      {"fresh database shows setup without writing",
       FreshDatabaseShowsSetupMenuWithoutWriting},
      {"create from scratch button creates Ready month",
       CreateFromScratchButtonCreatesReadyMonth},
      {"copy menu action creates month from previous users",
       CopyMenuActionCreatesMonthFromPreviousUsers},
      {"month state Error blocks writes and setup menu",
       MonthStateErrorBlocksWritesAndSetupMenu},
      {"calendar transition keeps only current setup request",
       CalendarTransitionShowsOnlyCurrentMissingMonth},
  }};

  int failed = 0;
  for (const TestCase& test : tests)
  {
    qInfo().noquote() << "RUN " << test.name;
    if (!test.run())
    {
      ++failed;
      qCritical().noquote() << "FAIL" << test.name;
      continue;
    }
    qInfo().noquote() << "PASS" << test.name;
  }

  qInfo() << "Tests:" << tests.size() << "failed:" << failed;
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
