#include <QCoreApplication>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <memory>
#include <utility>

#include "EventApp.hpp"
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

bool HasIndexColumns(QSqlDatabase& db, const QString& indexName,
                     const QStringList& expected)
{
  QSqlQuery query(db);
  if (!query.exec(QString("PRAGMA index_info(%1)").arg(indexName)))
  {
    return false;
  }
  QStringList actual;
  while (query.next())
  {
    actual.push_back(query.value(2).toString());
  }
  return actual == expected;
}

ParticipantId ParticipantUuid(const char* value)
{
  return {QString::fromLatin1(value)};
}

class RecordingEventStorage final : public IEventStorage
{
public:
  QString lastError() const override
  {
    return {};
  }

  std::optional<std::vector<EventRecord>> listEvents() override
  {
    return std::vector<EventRecord>();
  }

  std::optional<EventRecord> getEvent(const EventId&) override
  {
    return std::nullopt;
  }

  std::optional<ParticipantEventStatistics>
  participantStatistics(const ParticipantId&) override
  {
    return std::nullopt;
  }

  bool saveEvent(const EventRecord& event) override
  {
    saveCalled = true;
    savedCategory = event.category;
    return true;
  }

  bool removeEvent(const EventId&, qint64) override
  {
    return false;
  }

  bool saveCalled = false;
  EventCategory savedCategory = EventCategory::Unspecified;
};

EventRecord SampleEvent()
{
  const ParticipantId petya =
      ParticipantUuid("11111111-1111-1111-1111-111111111111");
  const ParticipantId nastya =
      ParticipantUuid("44444444-4444-4444-4444-444444444444");
  EventRecord event;
  event.id = CreateEventId();
  event.title = "Летний турнир";
  event.date = QDate(2026, 7, 18);
  event.category = EventCategory::ExternalCompetition;
  event.notes = "1 место Коля. Настя секундировала.";
  event.participants = {{petya, "Пётр", "Пётр Петров"}};
  event.nonCompetingAttendees =
      {{nastya, "Настя", "Анастасия Иванова"}};
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
  event.participants.front().displayNameSnapshot =
      QString(kMaxEventParticipantSnapshotNameLength, QChar(0x0418));
  if (!Check(event.isValid(), "300-character participant snapshot rejected"))
  {
    return false;
  }
  event.participants.front().displayNameSnapshot.append(QChar(0x0418));
  if (!Check(!event.isValid(), "301-character participant snapshot accepted"))
  {
    return false;
  }
  event = SampleEvent();
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
  if (!Check(!invalid.isValid(), "negative event revision was accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.category = static_cast<EventCategory>(99);
  if (!Check(!invalid.isValid(), "invalid event category was accepted"))
  {
    return false;
  }
  invalid = event;
  invalid.nonCompetingAttendees = invalid.participants;
  return Check(!invalid.isValid(),
               "participant was accepted as competitor and attendee");
}

bool EventAppRejectsUnclassifiedCategory()
{
  auto storage = std::make_unique<RecordingEventStorage>();
  RecordingEventStorage* storageObserver = storage.get();
  EventApp app(std::move(storage));

  EventRecord event = SampleEvent();
  event.category = EventCategory::Unspecified;
  if (!Check(!app.save(event),
             "EventApp accepted an unclassified event") ||
      !Check(!storageObserver->saveCalled,
             "EventApp delegated an unclassified event to storage"))
  {
    return false;
  }

  event.category = EventCategory::ClubTrainingTournament;
  event.nonCompetingAttendees.clear();
  return Check(app.save(event), "EventApp rejected a classified event") &&
         Check(storageObserver->saveCalled &&
                   storageObserver->savedCategory ==
                       EventCategory::ClubTrainingTournament,
               "EventApp did not delegate the classified event");
}

bool EventRoundTripTest(const QString& path)
{
  EventSqliteStorage storage;
  if (!Check(storage.open(path), "event database open failed"))
  {
    return false;
  }
  EventRecord unclassified = SampleEvent();
  unclassified.category = EventCategory::Unspecified;
  if (!Check(!storage.saveEvent(unclassified),
             "unclassified event was accepted for saving") ||
      !Check(storage.lastError().contains("category", Qt::CaseInsensitive),
             "unclassified event rejection has no useful error"))
  {
    return false;
  }
  EventRecord event = SampleEvent();
  const QString maxSnapshot(kMaxEventParticipantSnapshotNameLength,
                            QChar(0x0418));
  event.participants.front().displayNameSnapshot = maxSnapshot;
  event.participants.front().fullNameSnapshot = maxSnapshot;
  if (!Check(storage.saveEvent(event), "event save failed"))
  {
    return false;
  }
  const auto loaded = storage.getEvent(event.id);
  if (!Check(loaded.has_value(), "saved event missing") ||
       !Check(loaded->title == event.title && loaded->date == event.date &&
                  loaded->category == event.category &&
                  loaded->revision == 1 && loaded->notes == event.notes,
             "event metadata round-trip failed") ||
      !Check(loaded->participants.size() == 1 &&
                 loaded->participants.front().displayNameSnapshot ==
                     maxSnapshot &&
                 loaded->participants.front().fullNameSnapshot == maxSnapshot,
              "event participant snapshot round-trip failed") ||
      !Check(loaded->nonCompetingAttendees.size() == 1 &&
                 loaded->nonCompetingAttendees.front().displayNameSnapshot ==
                     "Настя",
             "event non-competing attendee round-trip failed") ||
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
  event.category = EventCategory::ClubTrainingTournament;
  event.nonCompetingAttendees.clear();
  event.participants.push_back({kolya, "Коля", QString()});
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
                  updated->category ==
                      EventCategory::ClubTrainingTournament &&
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

bool ParticipantStatisticsTest(const QString& path)
{
  EventSqliteStorage storage;
  if (!Check(storage.open(path), "statistics database open failed"))
  {
    return false;
  }

  const ParticipantId petya =
      ParticipantUuid("11111111-1111-1111-1111-111111111111");
  const ParticipantId nastya =
      ParticipantUuid("44444444-4444-4444-4444-444444444444");
  const ParticipantId kolya =
      ParticipantUuid("22222222-2222-2222-2222-222222222222");
  const ParticipantId absent =
      ParticipantUuid("33333333-3333-3333-3333-333333333333");

  EventRecord july = SampleEvent();

  EventRecord may = SampleEvent();
  may.id = CreateEventId();
  may.title = "Майский турнир";
  may.date = QDate(2026, 5, 3);
  may.category = EventCategory::ClubTrainingTournament;
  may.nonCompetingAttendees.clear();
  may.participants = {{kolya, "Коля", QString()},
                      {petya, "Пётр", "Пётр Петров"}};
  EventBout internalBout;
  internalBout.id = CreateBoutId();
  internalBout.sideA.participantId = kolya;
  internalBout.sideB.participantId = petya;
  internalBout.scoreA = 20;
  internalBout.scoreB = 17;
  EventBout externalBout;
  externalBout.id = CreateBoutId();
  externalBout.sideA.participantId = kolya;
  externalBout.sideB.freeName = "Гость";
  externalBout.scoreA = 20;
  externalBout.scoreB = 10;
  may.bouts = {internalBout, externalBout};

  EventRecord september = SampleEvent();
  september.id = CreateEventId();
  september.title = "Сентябрьский турнир";
  september.date = QDate(2026, 9, 12);
  september.category = EventCategory::SoftCombatTournament;
  september.nonCompetingAttendees.clear();
  september.bouts.clear();

  if (!Check(storage.saveEvent(july), "July statistics fixture failed") ||
      !Check(storage.saveEvent(may), "May statistics fixture failed") ||
      !Check(storage.saveEvent(september),
             "September statistics fixture failed"))
  {
    return false;
  }

  const auto petyaStatistics = storage.participantStatistics(petya);
  if (!Check(petyaStatistics.has_value(),
             "participant statistics query failed") ||
      !Check(petyaStatistics->participantId == petya &&
                  petyaStatistics->tournamentCount == 3 &&
                   petyaStatistics->clubTournamentCount == 1 &&
                   petyaStatistics->externalCompetitionCount == 1 &&
                   petyaStatistics->softCombatTournamentCount == 1 &&
                   petyaStatistics->unspecifiedTournamentCount == 0 &&
                   petyaStatistics->nonCompetingTripCount == 0 &&
                  petyaStatistics->boutCount == 2 &&
                 petyaStatistics->firstTournament == QDate(2026, 5, 3) &&
                 petyaStatistics->lastTournament == QDate(2026, 9, 12),
             "participant tournament statistics are incorrect"))
  {
    return false;
  }

  const auto kolyaStatistics = storage.participantStatistics(kolya);
  if (!Check(kolyaStatistics.has_value() &&
                  kolyaStatistics->tournamentCount == 1 &&
                   kolyaStatistics->clubTournamentCount == 1 &&
                   kolyaStatistics->externalCompetitionCount == 0 &&
                   kolyaStatistics->softCombatTournamentCount == 0 &&
                   kolyaStatistics->unspecifiedTournamentCount == 0 &&
                   kolyaStatistics->nonCompetingTripCount == 0 &&
                  kolyaStatistics->boutCount == 2 &&
                 kolyaStatistics->firstTournament == QDate(2026, 5, 3) &&
                 kolyaStatistics->lastTournament == QDate(2026, 5, 3),
             "distinct tournament or either-side bout count is incorrect"))
  {
    return false;
  }

  const auto nastyaStatistics = storage.participantStatistics(nastya);
  if (!Check(nastyaStatistics.has_value() &&
                 nastyaStatistics->tournamentCount == 0 &&
                 nastyaStatistics->externalCompetitionCount == 0 &&
                 nastyaStatistics->softCombatTournamentCount == 0 &&
                 nastyaStatistics->nonCompetingTripCount == 1 &&
                 nastyaStatistics->boutCount == 0 &&
                 !nastyaStatistics->firstTournament.has_value() &&
                 !nastyaStatistics->lastTournament.has_value(),
             "non-competing trip inflated tournament statistics"))
  {
    return false;
  }

  const auto absentStatistics = storage.participantStatistics(absent);
  if (!Check(absentStatistics.has_value() &&
                  absentStatistics->participantId == absent &&
                  absentStatistics->tournamentCount == 0 &&
                   absentStatistics->clubTournamentCount == 0 &&
                   absentStatistics->externalCompetitionCount == 0 &&
                   absentStatistics->softCombatTournamentCount == 0 &&
                   absentStatistics->unspecifiedTournamentCount == 0 &&
                   absentStatistics->nonCompetingTripCount == 0 &&
                  absentStatistics->boutCount == 0 &&
                 !absentStatistics->firstTournament.has_value() &&
                 !absentStatistics->lastTournament.has_value(),
             "participant without tournaments did not return zero statistics"))
  {
    return false;
  }

  return Check(!storage.participantStatistics({"invalid"}).has_value(),
               "invalid participant statistics ID was accepted");
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
  bool attendeesEmpty = false;
  bool boutsEmpty = false;
  bool participantHistoryIndexOk = false;
  bool attendeeHistoryIndexOk = false;
  bool categoryIndexOk = false;
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 5;
    participantsEmpty =
        query.exec("SELECT count(*) FROM event_participants") && query.next() &&
        query.value(0).toInt() == 0;
    attendeesEmpty = query.exec("SELECT count(*) FROM event_attendees") &&
                     query.next() && query.value(0).toInt() == 0;
    boutsEmpty = query.exec("SELECT count(*) FROM event_bouts") &&
                 query.next() && query.value(0).toInt() == 0;
    participantHistoryIndexOk = HasIndexColumns(
        db, "idx_event_participants_history", {"participant_id", "event_id"});
    attendeeHistoryIndexOk = HasIndexColumns(
        db, "idx_event_attendees_history", {"participant_id", "event_id"});
    categoryIndexOk = HasIndexColumns(
        db, "idx_events_category_date",
        {"event_category", "event_date", "title", "id"});
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && participantsEmpty && attendeesEmpty &&
                   boutsEmpty && participantHistoryIndexOk &&
                   attendeeHistoryIndexOk && categoryIndexOk,
               "event schema version or cascade contract failed");
}

bool CreateMigrationFixture(const QString& path, int schemaVersion,
                            int participantSnapshotNameMaxLength)
{
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  if (!db.open())
  {
    return false;
  }

  bool created = false;
  {
    QSqlQuery query(db);
    QStringList statements = {
        "CREATE TABLE events("
        "id TEXT PRIMARY KEY NOT NULL, "
        "title TEXT NOT NULL CHECK(length(trim(title)) BETWEEN 1 AND 200 AND "
        "instr(title, char(10)) = 0 AND instr(title, char(13)) = 0), "
        "event_date TEXT NOT NULL CHECK(length(event_date) = 10 AND "
        "date(event_date) IS NOT NULL AND date(event_date) = event_date AND "
        "CAST(substr(event_date, 1, 4) AS INTEGER) BETWEEN 1 AND 9999), "
        "revision INTEGER NOT NULL DEFAULT 1 "
        "CHECK(typeof(revision) = 'integer' AND revision >= 1), "
        "notes TEXT NOT NULL DEFAULT '' CHECK(length(notes) <= 32768), "
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)",
        QString(
            "CREATE TABLE event_participants("
            "event_id TEXT NOT NULL, participant_id TEXT NOT NULL, "
            "participant_name_snapshot TEXT NOT NULL "
            "CHECK(length(trim(participant_name_snapshot)) BETWEEN 1 AND "
            "%1 AND instr(participant_name_snapshot, char(10)) = 0 AND "
            "instr(participant_name_snapshot, char(13)) = 0), "
            "participant_full_name_snapshot TEXT NOT NULL DEFAULT '' "
            "CHECK(length(participant_full_name_snapshot) <= 300 AND "
            "instr(participant_full_name_snapshot, char(10)) = 0 AND "
            "instr(participant_full_name_snapshot, char(13)) = 0), "
            "sort_order INTEGER NOT NULL CHECK(sort_order >= 0), "
            "PRIMARY KEY(event_id, participant_id), "
            "UNIQUE(event_id, sort_order), "
            "FOREIGN KEY(event_id) REFERENCES events(id) ON DELETE CASCADE)")
            .arg(participantSnapshotNameMaxLength),
        "CREATE TABLE event_bouts("
        "id TEXT PRIMARY KEY NOT NULL, event_id TEXT NOT NULL, "
        "sort_order INTEGER NOT NULL CHECK(sort_order >= 0), "
        "side_a_participant_id TEXT, side_a_free_name TEXT, "
        "side_b_participant_id TEXT, side_b_free_name TEXT, "
        "score_a INTEGER NOT NULL CHECK(typeof(score_a) = 'integer' AND "
        "score_a BETWEEN 0 AND 2147483647), "
        "score_b INTEGER NOT NULL CHECK(typeof(score_b) = 'integer' AND "
        "score_b BETWEEN 0 AND 2147483647), "
        "UNIQUE(event_id, sort_order), "
        "FOREIGN KEY(event_id) REFERENCES events(id) ON DELETE CASCADE, "
        "FOREIGN KEY(event_id, side_a_participant_id) REFERENCES "
        "event_participants(event_id, participant_id), "
        "FOREIGN KEY(event_id, side_b_participant_id) REFERENCES "
        "event_participants(event_id, participant_id), "
        "CHECK((side_a_participant_id IS NOT NULL AND "
        "side_a_free_name IS NULL) OR (side_a_participant_id IS NULL AND "
        "side_a_free_name IS NOT NULL AND "
        "length(trim(side_a_free_name)) BETWEEN 1 AND 200 AND "
        "instr(side_a_free_name, char(10)) = 0 AND "
        "instr(side_a_free_name, char(13)) = 0)), "
        "CHECK((side_b_participant_id IS NOT NULL AND "
        "side_b_free_name IS NULL) OR (side_b_participant_id IS NULL AND "
        "side_b_free_name IS NOT NULL AND "
        "length(trim(side_b_free_name)) BETWEEN 1 AND 200 AND "
        "instr(side_b_free_name, char(10)) = 0 AND "
        "instr(side_b_free_name, char(13)) = 0)), "
        "CHECK(side_a_participant_id IS NULL OR "
        "side_b_participant_id IS NULL OR "
        "side_a_participant_id <> side_b_participant_id))",
        "CREATE INDEX idx_events_date ON "
        "events(event_date DESC, title, id)",
        "CREATE INDEX idx_event_bouts_side_a ON "
        "event_bouts(event_id, side_a_participant_id)",
        "CREATE INDEX idx_event_bouts_side_b ON "
        "event_bouts(event_id, side_b_participant_id)",
        "INSERT INTO events(id, title, event_date, revision, notes) VALUES("
        "'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', 'Legacy tournament', "
        "'2025-12-31', 7, 'Legacy notes')",
        "INSERT INTO event_participants("
        "event_id, participant_id, participant_name_snapshot, "
        "participant_full_name_snapshot, sort_order) VALUES("
        "'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', "
        "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb', "
        "'Legacy name', 'Legacy full name', 0)",
        "INSERT INTO event_bouts("
        "id, event_id, sort_order, side_a_participant_id, "
        "side_a_free_name, side_b_participant_id, side_b_free_name, "
        "score_a, score_b) VALUES("
        "'cccccccc-cccc-4ccc-8ccc-cccccccccccc', "
        "'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', 0, "
        "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb', NULL, NULL, "
        "'Legacy guest', 5, 20)",
        QString("PRAGMA user_version = %1").arg(schemaVersion)};
    if (schemaVersion >= 3)
    {
      statements.insert(
          6, "CREATE INDEX idx_event_participants_history ON "
             "event_participants(participant_id, event_id)");
    }

    created = query.exec("PRAGMA foreign_keys = ON") && db.transaction();
    for (const QString& statement : statements)
    {
      if (!created || !query.exec(statement))
      {
        created = false;
        break;
      }
    }
    if (created)
    {
      created = query.exec("PRAGMA foreign_key_check") && !query.next();
      query.finish();
    }
    if (!created || !db.commit())
    {
      db.rollback();
      created = false;
    }
  }

  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return created;
}

bool CreateSchemaV4Fixture(const QString& path)
{
  if (!CreateMigrationFixture(path, 3, 300))
  {
    return false;
  }
  const QString connection = QUuid::createUuid().toString();
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
  db.setDatabaseName(path);
  bool created = false;
  if (db.open() && db.transaction())
  {
    QSqlQuery query(db);
    created =
        query.exec(
            "ALTER TABLE events ADD COLUMN event_category INTEGER NOT NULL "
            "DEFAULT 0 CHECK(typeof(event_category) = 'integer' AND "
            "event_category BETWEEN 0 AND 2)") &&
        query.exec("CREATE INDEX idx_events_category_date ON "
                   "events(event_category, event_date DESC, title, id)") &&
        query.exec("PRAGMA user_version = 4") && db.commit();
    if (!created)
    {
      db.rollback();
    }
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return created;
}

bool SchemaMigrationV1ToV5Test(const QString& path)
{
  if (!Check(CreateMigrationFixture(path, 1, 200),
             "event schema v1 fixture creation failed"))
  {
    return false;
  }

  {
    EventSqliteStorage storage;
    const EventId eventId = {
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
    if (!Check(storage.open(path), "event schema v1 migration failed"))
    {
      return false;
    }
    const auto event = storage.getEvent(eventId);
    if (!Check(event.has_value() && event->title == "Legacy tournament" &&
                   event->date == QDate(2025, 12, 31) &&
                   event->category == EventCategory::Unspecified &&
                   event->revision == 7 && event->notes == "Legacy notes",
               "event metadata was lost during v1 migration") ||
        !Check(event.has_value() && event->participants.size() == 1 &&
                   event->participants.front().participantId.value ==
                       "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" &&
                   event->participants.front().displayNameSnapshot ==
                       "Legacy name" &&
                   event->participants.front().fullNameSnapshot ==
                       "Legacy full name",
               "event participant was lost during v1 migration") ||
        !Check(event.has_value() && event->bouts.size() == 1 &&
                   event->bouts.front().id.value ==
                       "cccccccc-cccc-4ccc-8ccc-cccccccccccc" &&
                   event->bouts.front().sideA.participantId.has_value() &&
                   event->bouts.front().sideA.participantId->value ==
                       "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" &&
                   event->bouts.front().sideB.freeName == "Legacy guest" &&
                   event->bouts.front().scoreA == 5 &&
                   event->bouts.front().scoreB == 20,
               "event bout was lost during v1 migration"))
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
  bool foreignKeysOk = false;
  bool snapshotLimitOk = false;
  bool participantHistoryIndexOk = false;
  bool categoryIndexOk = false;
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 5;
    query.finish();
    foreignKeysOk = query.exec("PRAGMA foreign_key_check") && !query.next();
    query.finish();
    snapshotLimitOk =
        query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' "
                   "AND name = 'event_participants'") &&
        query.next() &&
        query.value(0).toString().contains(
            "length(trim(participant_name_snapshot)) BETWEEN 1 AND 300",
            Qt::CaseInsensitive);
    participantHistoryIndexOk = HasIndexColumns(
        db, "idx_event_participants_history", {"participant_id", "event_id"});
    categoryIndexOk = HasIndexColumns(
        db, "idx_events_category_date",
        {"event_category", "event_date", "title", "id"});
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && foreignKeysOk && snapshotLimitOk &&
                   participantHistoryIndexOk && categoryIndexOk,
               "event schema v1 migration postconditions failed");
}

bool SchemaMigrationV2ToV5Test(const QString& path)
{
  if (!Check(CreateMigrationFixture(path, 2, 300),
             "event schema v2 fixture creation failed"))
  {
    return false;
  }

  const ParticipantId participant =
      ParticipantUuid("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
  {
    EventSqliteStorage storage;
    if (!Check(storage.open(path), "event schema v2 migration failed"))
    {
      return false;
    }
    const auto statistics = storage.participantStatistics(participant);
    if (!Check(statistics.has_value() &&
                   statistics->participantId == participant &&
                   statistics->tournamentCount == 1 &&
                   statistics->clubTournamentCount == 0 &&
                   statistics->externalCompetitionCount == 0 &&
                   statistics->unspecifiedTournamentCount == 1 &&
                   statistics->boutCount == 1 &&
                   statistics->firstTournament == QDate(2025, 12, 31) &&
                   statistics->lastTournament == QDate(2025, 12, 31),
               "statistics were lost during event schema v2 migration"))
    {
      return false;
    }
    const EventId eventId = {
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
    const auto legacyEvent = storage.getEvent(eventId);
    if (!Check(legacyEvent.has_value() &&
                   legacyEvent->category == EventCategory::Unspecified,
               "legacy event did not preserve unspecified category") ||
        !Check(!storage.saveEvent(*legacyEvent),
               "legacy event was saved without explicit classification"))
    {
      return false;
    }
    EventRecord classified = *legacyEvent;
    classified.category = EventCategory::ExternalCompetition;
    if (!Check(storage.saveEvent(classified),
               "legacy event could not be explicitly classified"))
    {
      return false;
    }
    const auto reloaded = storage.getEvent(eventId);
    if (!Check(reloaded.has_value() &&
                   reloaded->category == EventCategory::ExternalCompetition,
               "explicit legacy event classification was not persisted"))
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
  bool foreignKeysOk = false;
  bool participantHistoryIndexOk = false;
  bool categoryIndexOk = false;
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 5;
    query.finish();
    foreignKeysOk = query.exec("PRAGMA foreign_key_check") && !query.next();
    participantHistoryIndexOk = HasIndexColumns(
        db, "idx_event_participants_history", {"participant_id", "event_id"});
    categoryIndexOk = HasIndexColumns(
        db, "idx_events_category_date",
        {"event_category", "event_date", "title", "id"});
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && foreignKeysOk && participantHistoryIndexOk &&
                   categoryIndexOk,
               "event schema v2 migration postconditions failed");
}

bool SchemaMigrationV3ToV5Test(const QString& path)
{
  if (!Check(CreateMigrationFixture(path, 3, 300),
             "event schema v3 fixture creation failed"))
  {
    return false;
  }

  const EventId eventId = {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
  {
    EventSqliteStorage storage;
    if (!Check(storage.open(path), "event schema v3 migration failed"))
    {
      return false;
    }
    const auto event = storage.getEvent(eventId);
    if (!Check(event.has_value() &&
                   event->category == EventCategory::Unspecified,
               "v3 event was assigned a fabricated category"))
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
  bool categoryIndexOk = false;
  {
    QSqlQuery query(db);
    versionOk = query.exec("PRAGMA user_version") && query.next() &&
                query.value(0).toInt() == 5;
    categoryIndexOk = HasIndexColumns(
        db, "idx_events_category_date",
        {"event_category", "event_date", "title", "id"});
  }
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && categoryIndexOk,
               "event schema v3 migration postconditions failed");
}

bool SchemaMigrationV4ToV5Test(const QString& path)
{
  if (!Check(CreateSchemaV4Fixture(path),
             "event schema v4 fixture creation failed"))
  {
    return false;
  }
  const EventId eventId = {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
  {
    EventSqliteStorage storage;
    if (!Check(storage.open(path), "event schema v4 migration failed"))
    {
      return false;
    }
    const auto event = storage.getEvent(eventId);
    if (!Check(event.has_value() &&
                   event->category == EventCategory::Unspecified &&
                   event->participants.size() == 1 &&
                   event->nonCompetingAttendees.empty() &&
                   event->bouts.size() == 1,
               "v4 event data was lost during migration"))
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
  QSqlQuery query(db);
  const bool versionOk = query.exec("PRAGMA user_version") && query.next() &&
                         query.value(0).toInt() == 5;
  const bool foreignKeysOk = query.exec("PRAGMA foreign_key_check") &&
                             !query.next();
  const bool attendeeIndexOk = HasIndexColumns(
      db, "idx_event_attendees_history", {"participant_id", "event_id"});
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection);
  return Check(versionOk && foreignKeysOk && attendeeIndexOk,
               "event schema v4 migration postconditions failed");
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

bool EventCategoryContractNearMissesAreRejected(const QString& pathPrefix)
{
  const QString canonical =
      "event_category INTEGER NOT NULL DEFAULT 0 "
      "CHECK(typeof(event_category) = 'integer' AND "
      "event_category BETWEEN 0 AND 3)";
  struct Mutation
  {
    const char* suffix;
    QString replacement;
  };
  const std::vector<Mutation> mutations = {
      {"nullable",
       "event_category INTEGER DEFAULT 0 "
       "CHECK(typeof(event_category) = 'integer' AND "
       "event_category BETWEEN 0 AND 3)"},
      {"wrong-type",
       "event_category TEXT NOT NULL DEFAULT 0 "
       "CHECK(typeof(event_category) = 'integer' AND "
       "event_category BETWEEN 0 AND 3)"},
      {"wrong-default",
       "event_category INTEGER NOT NULL DEFAULT 1 "
       "CHECK(typeof(event_category) = 'integer' AND "
       "event_category BETWEEN 0 AND 3)"},
      {"missing-typeof",
       "event_category INTEGER NOT NULL DEFAULT 0 "
       "CHECK(event_category BETWEEN 0 AND 3)"}};

  for (const Mutation& mutation : mutations)
  {
    const QString path =
        QString("%1-%2.db")
            .arg(pathPrefix, QString::fromLatin1(mutation.suffix));
    {
      EventSqliteStorage storage;
      if (!Check(storage.open(path),
                 "category near-miss database creation failed"))
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
      if (query.exec("SELECT sql FROM sqlite_master WHERE type = 'table' "
                     "AND name = 'events'") &&
          query.next())
      {
        definition = query.value(0).toString().simplified();
      }
      const bool hadCanonical = definition.contains(canonical);
      definition.replace(canonical, mutation.replacement);
      changed = hadCanonical && !definition.contains(canonical) &&
                query.exec("PRAGMA foreign_keys = OFF") &&
                query.exec("DROP TABLE events") && query.exec(definition) &&
                query.exec("CREATE INDEX idx_events_date ON "
                           "events(event_date DESC, title, id)") &&
                query.exec("CREATE INDEX idx_events_category_date ON "
                           "events(event_category, event_date DESC, title, "
                           "id)");
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
    if (!Check(changed, "event category near-miss mutation failed"))
    {
      return false;
    }

    EventSqliteStorage storage;
    if (!Check(!storage.open(path),
               "event category near-miss schema was accepted") ||
        !Check(storage.lastError().contains("category",
                                            Qt::CaseInsensitive) ||
                   storage.lastError().contains("constraints",
                                                Qt::CaseInsensitive),
               "event category near-miss rejection has no useful error"))
    {
      return false;
    }
  }
  return true;
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
      EventAppRejectsUnclassifiedCategory() &&
      EventRoundTripTest(directory.filePath("events.db")) &&
      ParticipantStatisticsTest(directory.filePath("statistics-events.db")) &&
      TransactionRollbackTest(directory.filePath("rollback-events.db")) &&
      SchemaAndCascadeTest(directory.filePath("cascade-events.db")) &&
      SchemaMigrationV1ToV5Test(
          directory.filePath("migration-v1-events.db")) &&
      SchemaMigrationV2ToV5Test(
          directory.filePath("migration-v2-events.db")) &&
      SchemaMigrationV3ToV5Test(
          directory.filePath("migration-v3-events.db")) &&
      SchemaMigrationV4ToV5Test(
          directory.filePath("migration-v4-events.db")) &&
      MalformedSchemaIsRejected(directory.filePath("malformed-events.db")) &&
      NearMissConstraintIsRejected(
          directory.filePath("constraint-near-miss.db")) &&
      EventCategoryContractNearMissesAreRejected(
          directory.filePath("category-contract-near-miss")) &&
      NearMissIndexIsRejected(directory.filePath("index-near-miss.db"));
  return ok ? 0 : 1;
}
