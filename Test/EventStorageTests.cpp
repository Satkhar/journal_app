#include <QCoreApplication>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include "EventSqliteStorage.hpp"

namespace
{

bool Check(bool condition, const char* message)
{
  if (!condition)
  {
    qCritical() << message;
  }
  return condition;
}

ParticipantId ParticipantUuid(const char* value)
{
  return {QString::fromLatin1(value)};
}

EventRecord SampleEvent()
{
  const ParticipantId petya =
      ParticipantUuid("11111111-1111-1111-1111-111111111111");
  EventRecord event;
  event.id = CreateEventId();
  event.title = "Летний турнир";
  event.date = QDate(2026, 7, 18);
  event.notes = "1 место Коля. Настя секундировала.";
  event.participants = {{petya, "Пётр", "Пётр Петров"}};
  EventBout bout;
  bout.id = CreateBoutId();
  bout.sideA.participantId = petya;
  bout.sideB.freeName = "Вася из другого клуба";
  bout.scoreA = 5;
  bout.scoreB = 20;
  event.bouts = {bout};
  return event;
}

bool DomainValidationTest()
{
  EventRecord event = SampleEvent();
  EventRecord invalid = event;
  invalid.bouts.front().scoreA = -1;
  if (!Check(!invalid.isValid(), "negative event score was accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.bouts.front().sideB = invalid.bouts.front().sideA;
  if (!Check(!invalid.isValid(), "self-bout was accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.bouts.front().sideB = BoutSideRef();
  if (!Check(!invalid.isValid(), "empty free event side was accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.bouts.push_back(invalid.bouts.front());
  if (!Check(!invalid.isValid(), "duplicate bout IDs were accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.revision = -1;
  return Check(!invalid.isValid(), "negative event revision was accepted");
}

bool EventRoundTripTest(const QString& path)
{
  EventSqliteStorage storage;
  if (!Check(storage.open(path), "event database open failed"))
  {
    return false;
  }
  EventRecord event = SampleEvent();
  if (!Check(storage.saveEvent(event), "event save failed"))
  {
    return false;
  }
  const auto loaded = storage.getEvent(event.id);
  if (!Check(loaded.has_value(), "saved event missing") ||
      !Check(loaded->title == event.title && loaded->date == event.date &&
                 loaded->revision == 1 && loaded->notes == event.notes,
             "event metadata round-trip failed") ||
      !Check(loaded->participants.size() == 1 &&
                 loaded->participants.front().displayNameSnapshot == "Пётр" &&
                 loaded->participants.front().fullNameSnapshot ==
                     "Пётр Петров",
             "event participant snapshot round-trip failed") ||
      !Check(loaded->bouts.size() == 1 &&
                 loaded->bouts.front().sideA.participantId.has_value() &&
                 loaded->bouts.front().sideB.freeName ==
                     "Вася из другого клуба" &&
                 loaded->bouts.front().scoreA == 5 &&
                 loaded->bouts.front().scoreB == 20,
             "event bout round-trip failed"))
  {
    return false;
  }

  event = *loaded;
  const ParticipantId kolya =
      ParticipantUuid("22222222-2222-2222-2222-222222222222");
  event.notes = "Петины ошибки разобрать на тренировке";
  event.participants.push_back({kolya, "Коля"});
  EventBout replacement;
  replacement.id = CreateBoutId();
  replacement.sideA.participantId = event.participants.front().participantId;
  replacement.sideB.participantId = kolya;
  replacement.scoreA = 20;
  replacement.scoreB = 17;
  event.bouts = {replacement};
  if (!Check(storage.saveEvent(event), "event aggregate update failed"))
  {
    return false;
  }
  const auto updated = storage.getEvent(event.id);
  if (!Check(updated.has_value() && updated->bouts.size() == 1 &&
                 updated->bouts.front().id.value == replacement.id.value &&
                 updated->participants.size() == 2 &&
                 updated->revision == 2,
             "event update left stale aggregate rows"))
  {
    return false;
  }

  EventRecord stale = event;
  stale.title = "Устаревшее изменение";
  if (!Check(!storage.saveEvent(stale),
             "stale event revision overwrote a newer aggregate"))
  {
    return false;
  }
  if (!Check(!storage.removeEvent(event.id, event.revision),
             "stale event revision deleted a newer aggregate") ||
      !Check(storage.getEvent(event.id).has_value(),
             "failed stale delete removed the event"))
  {
    return false;
  }

  EventRecord invalid = *updated;
  invalid.bouts.front().sideB.participantId =
      ParticipantUuid("33333333-3333-3333-3333-333333333333");
  invalid.bouts.front().sideB.freeName.clear();
  if (!Check(!storage.saveEvent(invalid),
             "dangling internal event participant was accepted"))
  {
    return false;
  }
  const auto afterInvalidSave = storage.getEvent(event.id);
  if (!Check(afterInvalidSave.has_value() &&
                 afterInvalidSave->notes == updated->notes &&
                 afterInvalidSave->revision == updated->revision,
             "failed event validation changed previous aggregate"))
  {
    return false;
  }

  const auto listed = storage.listEvents();
  if (!Check(listed.has_value() && listed->size() == 1,
             "event directory returned wrong count") ||
      !Check(storage.removeEvent(event.id, afterInvalidSave->revision),
             "event removal failed"))
  {
    return false;
  }
  const auto afterRemoval = storage.listEvents();
  return Check(afterRemoval.has_value() && afterRemoval->empty(),
               "event removal left event rows");
}

bool TransactionRollbackTest(const QString& path)
{
  EventSqliteStorage storage;
  EventRecord first = SampleEvent();
  EventRecord second = SampleEvent();
  second.id = CreateEventId();
  second.title = "Второй турнир";
  second.bouts.front().id = CreateBoutId();
  if (!Check(storage.open(path), "rollback database open failed") ||
      !Check(storage.saveEvent(first), "first rollback fixture save failed") ||
      !Check(storage.saveEvent(second), "second rollback fixture save failed"))
  {
    return false;
  }

  const auto loadedFirst = storage.getEvent(first.id);
  const auto loadedSecond = storage.getEvent(second.id);
  if (!Check(loadedFirst.has_value() && loadedSecond.has_value(),
             "rollback fixtures are unreadable"))
  {
    return false;
  }
  EventRecord conflicting = *loadedFirst;
  conflicting.title = "Это изменение должно откатиться";
  conflicting.bouts.front().id = loadedSecond->bouts.front().id;
  if (!Check(!storage.saveEvent(conflicting),
             "cross-event bout ID conflict was accepted"))
  {
    return false;
  }
  const auto restored = storage.getEvent(first.id);
  return Check(restored.has_value() && restored->title == loadedFirst->title &&
                   restored->revision == loadedFirst->revision &&
                   restored->bouts.size() == 1 &&
                   restored->bouts.front().id.value ==
                       loadedFirst->bouts.front().id.value,
               "failed event transaction did not restore old aggregate");
}

bool SchemaAndCascadeTest(const QString& path)
{
  {
    EventSqliteStorage storage;
    EventRecord event = SampleEvent();
    if (!Check(storage.open(path), "cascade database open failed") ||
        !Check(storage.saveEvent(event), "cascade fixture save failed"))
    {
      return false;
    }
    const auto saved = storage.getEvent(event.id);
    if (!Check(saved.has_value(), "cascade fixture read failed") ||
        !Check(storage.removeEvent(event.id, saved->revision),
               "cascade event delete failed"))
    {
      return false;
    }
  }

  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool versionOk = false;
  bool participantsEmpty = false;
  bool boutsEmpty = false;
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 1;
    participantsEmpty =
        query.exec("SELECT count(*) FROM event_participants") && query.next() &&
        query.value(0).toInt() == 0;
    boutsEmpty = query.exec("SELECT count(*) FROM event_bouts") &&
                 query.next() && query.value(0).toInt() == 0;
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && participantsEmpty && boutsEmpty,
               "event schema version or cascade contract failed");
}

bool MalformedSchemaIsRejected(const QString& path)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool created = true;
  {
    QSqlQuery query(db);
    const QStringList statements = {
        "CREATE TABLE events(id TEXT, title TEXT, event_date TEXT, revision "
        "INTEGER, notes TEXT, created_at TEXT, updated_at TEXT)",
        "CREATE TABLE event_participants(event_id TEXT, participant_id TEXT, "
        "participant_name_snapshot TEXT, participant_full_name_snapshot "
        "TEXT, sort_order INTEGER)",
        "CREATE TABLE event_bouts(id TEXT, event_id TEXT, sort_order INTEGER, "
        "side_a_participant_id TEXT, side_a_free_name TEXT, "
        "side_b_participant_id TEXT, side_b_free_name TEXT, score_a INTEGER, "
        "score_b INTEGER)",
        "PRAGMA user_version = 1"};
    for (const QString& statement : statements)
    {
      if (!query.exec(statement))
      {
        created = false;
        break;
      }
    }
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  if (!created)
  {
    return false;
  }

  EventSqliteStorage storage;
  return Check(!storage.open(path), "malformed event schema v1 was accepted") &&
         Check(storage.lastError().contains("constraints",
                                            Qt::CaseInsensitive),
               "malformed event schema rejection has no useful error");
}

bool NearMissConstraintIsRejected(const QString& path)
{
  {
    EventSqliteStorage storage;
    if (!Check(storage.open(path), "near-miss database creation failed"))
    {
      return false;
    }
  }

  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool changed = false;
  {
    QSqlQuery query(db);
    QString definition;
    if (query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' AND "
                   "name = 'event_bouts'") &&
        query.next())
    {
      definition = query.value(0).toString();
    }
    const QString scoreConstraint =
        "score_b INTEGER NOT NULL CHECK(typeof(score_b) = 'integer' AND "
        "score_b BETWEEN 0 AND 2147483647)";
    const bool hadConstraint = definition.contains(scoreConstraint);
    definition.replace(scoreConstraint, "score_b INTEGER NOT NULL");
    changed = hadConstraint && !definition.contains(scoreConstraint) &&
              query.exec("DROP TABLE event_bouts") &&
              query.exec(definition) &&
              query.exec("CREATE INDEX idx_event_bouts_side_a ON "
                         "event_bouts(event_id, side_a_participant_id)") &&
              query.exec("CREATE INDEX idx_event_bouts_side_b ON "
                         "event_bouts(event_id, side_b_participant_id)");
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  if (!Check(changed, "near-miss schema mutation failed"))
  {
    return false;
  }

  EventSqliteStorage storage;
  return Check(!storage.open(path),
               "event schema without score_b constraint was accepted");
}

bool NearMissIndexIsRejected(const QString& path)
{
  {
    EventSqliteStorage storage;
    if (!Check(storage.open(path), "index near-miss database creation failed"))
    {
      return false;
    }
  }

  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }
  bool changed = false;
  {
    QSqlQuery query(db);
    changed = query.exec("DROP INDEX idx_event_bouts_side_b") &&
              query.exec("CREATE INDEX idx_event_bouts_side_b ON "
                         "event_bouts(side_b_participant_id, event_id)");
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  if (!Check(changed, "near-miss index mutation failed"))
  {
    return false;
  }

  EventSqliteStorage storage;
  return Check(!storage.open(path),
               "event schema with wrong index definition was accepted");
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!Check(directory.isValid(), "temporary directory creation failed"))
  {
    return 1;
  }
  const bool ok =
      DomainValidationTest() &&
      EventRoundTripTest(directory.filePath("events.db")) &&
      TransactionRollbackTest(directory.filePath("rollback-events.db")) &&
      SchemaAndCascadeTest(directory.filePath("cascade-events.db")) &&
      MalformedSchemaIsRejected(directory.filePath("malformed-events.db")) &&
      NearMissConstraintIsRejected(
          directory.filePath("constraint-near-miss.db")) &&
      NearMissIndexIsRejected(directory.filePath("index-near-miss.db"));
  return ok ? 0 : 1;
}
