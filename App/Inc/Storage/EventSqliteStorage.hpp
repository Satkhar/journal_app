#pragma once

#include <QSqlDatabase>

#include "IEventStorage.hpp"

// Владеет отдельным named connection к БД турниров. Aggregate events имеет
// собственную revision/CAS семантику и не входит в month sync.
class EventSqliteStorage : public IEventStorage
{
public:
  EventSqliteStorage();
  ~EventSqliteStorage() override;
  EventSqliteStorage(const EventSqliteStorage&) = delete;
  EventSqliteStorage& operator=(const EventSqliteStorage&) = delete;
  EventSqliteStorage(EventSqliteStorage&&) = delete;
  EventSqliteStorage& operator=(EventSqliteStorage&&) = delete;

  bool open(const QString& path);
  QString lastError() const override;
  std::optional<std::vector<EventRecord>> listEvents() override;
  std::optional<EventRecord> getEvent(const EventId& id) override;
  bool saveEvent(const EventRecord& event) override;
  bool removeEvent(const EventId& id, qint64 expectedRevision) override;

private:
  QSqlDatabase db_;
  QString connectionName_;
  QString lastError_;

  bool enableForeignKeys();
  bool ensureSchema();
  bool createSchema();
  bool migrateSchemaV1ToV2();
  bool verifySchema(int participantSnapshotNameMaxLength);
  std::optional<EventRecord> loadEvent(const EventId& id);
  bool tableExists(const QString& name) const;
  void setError(const QString& error);
};
