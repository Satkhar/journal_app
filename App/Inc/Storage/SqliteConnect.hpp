#pragma once

#include <QSqlDatabase>
#include <QString>

#include "IJournalStorage.hpp"

// Владеет уникальным named QSqlDatabase connection. Qt connection привязан к
// потоку создания; копирование/перемещение запрещено, чтобы два деструктора не
// закрыли один global connection registry entry.
class SqliteConnect
{
public:
  SqliteConnect();
  ~SqliteConnect();
  SqliteConnect(const SqliteConnect&) = delete;
  SqliteConnect& operator=(const SqliteConnect&) = delete;
  SqliteConnect(SqliteConnect&&) = delete;
  SqliteConnect& operator=(SqliteConnect&&) = delete;

  bool open(const QString& dbPath);
  QString lastError() const;
  std::optional<std::vector<JournalMonth>> listMonths();
  MonthSnapshot loadMonthSnapshot(int year, int month);
  MonthStateResult getMonthState(int year, int month);

  std::vector<Participant> getParticipantsForMonth(int year, int month);
  QVector<int> getActiveDays(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  std::vector<AttendanceRecord> getMonth(int year, int month);
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data);
  std::vector<ParticipantDayMarker> getDayMarkers(int year, int month);
  bool saveDayMarker(int year, int month, const ParticipantDayMarker& marker);
  bool removeDayMarker(int year, int month, const ParticipantId& participantId,
                       int day);
  bool addParticipantToMonth(int year, int month,
                             const ParticipantProfile& profile);
  bool removeParticipantFromMonth(int year, int month, const ParticipantId& id);
  bool replaceMonth(int year, int month, const MonthSnapshot& snapshot);
  std::optional<ParticipantProfile>
  getParticipantProfile(const ParticipantId& id);
  std::optional<ParticipantJournalStatistics>
  participantStatistics(const ParticipantId& id);
  std::optional<std::vector<ParticipantProfile>>
  listParticipantProfiles(bool includeArchived);
  bool updateParticipantProfile(const ParticipantProfile& profile);
  bool updateParticipantCard(const ParticipantCardUpdate& update);
  std::optional<ParticipantEmblem>
  getParticipantEmblem(const ParticipantId& id);
  bool saveParticipantEmblem(const ParticipantEmblem& emblem);
  bool removeParticipantEmblem(const ParticipantId& id,
                               qint64 expectedRevision);
  std::optional<std::vector<TimedStrikeTest>>
  timedStrikeTests(const ParticipantId& id);
  bool saveTimedStrikeTest(const TimedStrikeTest& record);
  bool removeTimedStrikeTest(const TimedStrikeTestId& id,
                             qint64 expectedRevision);
  bool setParticipantArchived(const ParticipantId& id, bool archived);

private:
  QSqlDatabase db_;
  QString connectionName_;
  QString lastError_;

  bool ensureSchema();
  bool createSchemaV3();
  bool createSchemaV4();
  bool createSchemaV5();
  bool createSchemaV7();
  bool createSchemaV8();
  bool createSchemaV9();
  bool createSchemaV10();
  bool createSchemaV11();
  bool createDayMarkerSchema();
  bool createRankSchema();
  bool createParticipantDetailsSchema();
  bool createParticipantNameSchema();
  bool createCombatHandSchema();
  bool createTrainingStartSchema();
  bool createParticipantMeasurementsSchema();
  bool upgradeDayMarkerKindsSchema();
  bool migrateLegacyUsersToV3();
  bool cleanupLegacyTables();
  bool migrateSchemaV2ToV3();
  bool migrateSchemaV3ToV4();
  bool migrateSchemaV4ToV5();
  bool migrateSchemaV5ToV7();
  bool migrateSchemaV6ToV7();
  bool migrateSchemaV7ToV8();
  bool migrateSchemaV8ToV9();
  bool migrateSchemaV9ToV10();
  bool migrateSchemaV10ToV11();
  bool createProfileValidationTriggers();
  bool verifySchemaV3();
  bool verifySchemaV4();
  bool verifySchemaV5();
  bool verifySchemaV7();
  bool verifySchemaV8();
  bool verifySchemaV9();
  bool verifySchemaV10();
  bool verifySchemaV11();
  bool tableExists(const QString& tableName) const;
  bool enableForeignKeys();
  bool validateYearMonth(int year, int month) const;
  bool validateSnapshot(int year, int month, const MonthSnapshot& snapshot);
  QVector<int> fullMonthDays(int year, int month) const;
  QVector<int> normalizeDays(int year, int month,
                             const QVector<int>& days) const;
  int daysInMonth(int year, int month) const;
  bool saveParticipantEmblemRecord(const ParticipantEmblem& emblem);
  bool removeParticipantEmblemRecord(const ParticipantId& id,
                                     qint64 expectedRevision);
  void setError(const QString& error);
};
