#include <QCoreApplication>
#include <QDebug>

#include "RemoteConnectionOptions.hpp"
#include "SyncService.hpp"

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

class FakeMonthStore : public IMonthSnapshotStore
{
public:
  MonthSnapshot loaded;
  MonthSnapshot replaced;
  QString error;
  bool replaceResult{true};
  int loadCalls{0};
  int replaceCalls{0};
  int replacedYear{0};
  int replacedMonth{0};

  MonthSnapshot loadMonthSnapshot(int, int) override
  {
    ++loadCalls;
    return loaded;
  }

  bool replaceMonth(int year, int month,
                    const MonthSnapshot& snapshot) override
  {
    ++replaceCalls;
    replacedYear = year;
    replacedMonth = month;
    replaced = snapshot;
    return replaceResult;
  }

  QString lastError() const override
  {
    return error;
  }
};

MonthSnapshot ReadySnapshot()
{
  MonthSnapshot snapshot;
  snapshot.state = MonthState::Ready;
  snapshot.activeDays = {1, 2};
  return snapshot;
}

bool PushUsesInjectedRemote()
{
  FakeMonthStore remote;
  const MonthSnapshot snapshot = ReadySnapshot();
  QString error = "stale";
  SyncService sync;

  TEST_CHECK(sync.pushMonthToServer(remote, 2026, 7, snapshot, &error));
  TEST_CHECK(error.isEmpty());
  TEST_CHECK(remote.replaceCalls == 1);
  TEST_CHECK(remote.replacedYear == 2026 && remote.replacedMonth == 7);
  TEST_CHECK(remote.replaced.activeDays == snapshot.activeDays);
  return true;
}

bool PushPropagatesRemoteError()
{
  FakeMonthStore remote;
  remote.replaceResult = false;
  remote.error = "remote write failed";
  QString error;
  SyncService sync;

  TEST_CHECK(!sync.pushMonthToServer(remote, 2026, 7, ReadySnapshot(),
                                     &error));
  TEST_CHECK(error == remote.error);
  return true;
}

bool PushRejectsMissingLocalSnapshot()
{
  FakeMonthStore remote;
  MonthSnapshot snapshot;
  snapshot.state = MonthState::Missing;
  QString error;
  SyncService sync;

  TEST_CHECK(!sync.pushMonthToServer(remote, 2026, 7, snapshot, &error));
  TEST_CHECK(remote.replaceCalls == 0);
  TEST_CHECK(error.contains("not ready", Qt::CaseInsensitive));
  return true;
}

bool MissingRemoteDoesNotModifyLocal()
{
  FakeMonthStore remote;
  remote.loaded.state = MonthState::Missing;
  FakeMonthStore local;
  QString error;
  SyncService sync;

  TEST_CHECK(!sync.pullMonthToLocal(remote, 2026, 7, local, &error));
  TEST_CHECK(local.replaceCalls == 0);
  TEST_CHECK(error.contains("missing", Qt::CaseInsensitive));
  return true;
}

bool RemoteErrorDoesNotModifyLocal()
{
  FakeMonthStore remote;
  remote.loaded.state = MonthState::Error;
  remote.loaded.errorMessage = "remote read failed";
  FakeMonthStore local;
  QString error;
  SyncService sync;

  TEST_CHECK(!sync.pullMonthToLocal(remote, 2026, 7, local, &error));
  TEST_CHECK(local.replaceCalls == 0);
  TEST_CHECK(error == remote.loaded.errorMessage);
  return true;
}

bool PullReplacesLocalOnce()
{
  FakeMonthStore remote;
  remote.loaded = ReadySnapshot();
  FakeMonthStore local;
  QString error;
  SyncService sync;

  TEST_CHECK(sync.pullMonthToLocal(remote, 2026, 7, local, &error));
  TEST_CHECK(error.isEmpty());
  TEST_CHECK(remote.loadCalls == 1);
  TEST_CHECK(local.replaceCalls == 1);
  TEST_CHECK(local.replaced.activeDays == remote.loaded.activeDays);
  return true;
}

bool PullPropagatesLocalError()
{
  FakeMonthStore remote;
  remote.loaded = ReadySnapshot();
  FakeMonthStore local;
  local.replaceResult = false;
  local.error = "local transaction failed";
  QString error;
  SyncService sync;

  TEST_CHECK(!sync.pullMonthToLocal(remote, 2026, 7, local, &error));
  TEST_CHECK(local.replaceCalls == 1);
  TEST_CHECK(error == local.error);
  return true;
}

bool RemoteOptionsEnforceTransportSafety()
{
  QString error;
  auto options = NormalizeRemoteConnectionOptions(
      {" http://127.0.0.1:8080/ ", QString(), 5000, false}, &error);
  TEST_CHECK(options.has_value());
  TEST_CHECK(options->baseUrl == "http://127.0.0.1:8080");
  TEST_CHECK(error.isEmpty());

  options = NormalizeRemoteConnectionOptions(
      {"http://192.168.1.5:8080", QString(), 5000, false}, &error);
  TEST_CHECK(!options.has_value());
  TEST_CHECK(error.contains("loopback", Qt::CaseInsensitive));

  options = NormalizeRemoteConnectionOptions(
      {"http://192.168.1.5:8080", QString(), 5000, true}, &error);
  TEST_CHECK(options.has_value());

  options = NormalizeRemoteConnectionOptions(
      {"https://user:secret@example.com", QString(), 5000, false}, &error);
  TEST_CHECK(!options.has_value());

  options = NormalizeRemoteConnectionOptions(
      {"https://example.com?token=secret", QString(), 5000, false}, &error);
  TEST_CHECK(!options.has_value());

  options = NormalizeRemoteConnectionOptions(
      {"https://example.com", "bad\r\ntoken", 5000, false}, &error);
  TEST_CHECK(!options.has_value());
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
      {"push uses injected remote", PushUsesInjectedRemote},
      {"push propagates remote error", PushPropagatesRemoteError},
      {"push rejects missing local snapshot", PushRejectsMissingLocalSnapshot},
      {"missing remote preserves local", MissingRemoteDoesNotModifyLocal},
      {"remote error preserves local", RemoteErrorDoesNotModifyLocal},
      {"pull replaces local once", PullReplacesLocalOnce},
      {"pull propagates local error", PullPropagatesLocalError},
      {"remote options enforce transport safety",
       RemoteOptionsEnforceTransportSafety},
  };

  for (const auto& test : tests)
  {
    if (!test.run())
    {
      qCritical() << "FAILED:" << test.name;
      return 1;
    }
    qInfo() << "PASSED:" << test.name;
  }
  return 0;
}
