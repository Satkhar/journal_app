#pragma once

#include <QSqlDatabase>
#include <QString>

#include "IJournalStorage.hpp"

class SqliteConnect
{
public:
  SqliteConnect();
  ~SqliteConnect();

  bool open(const QString& dbPath);
  QString lastError() const;
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
                             const Participant& participant);
  bool removeParticipantFromMonth(int year, int month, const ParticipantId& id);
  bool replaceMonth(int year, int month, const MonthSnapshot& snapshot);
  std::optional<ParticipantProfile>
  getParticipantProfile(const ParticipantId& id);
  std::optional<std::vector<ParticipantProfile>>
  listParticipantProfiles(bool includeArchived);
  bool updateParticipantProfile(const ParticipantProfile& profile);
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
  bool createDayMarkerSchema();
  bool createRankSchema();
  bool createParticipantDetailsSchema();
  bool upgradeDayMarkerKindsSchema();
  bool migrateLegacyUsersToV3();
  bool cleanupLegacyTables();
  bool migrateSchemaV2ToV3();
  bool migrateSchemaV3ToV4();
  bool migrateSchemaV4ToV5();
  bool migrateSchemaV5ToV7();
  bool migrateSchemaV6ToV7();
  bool createProfileValidationTriggers();
  bool verifySchemaV3();
  bool verifySchemaV4();
  bool verifySchemaV5();
  bool verifySchemaV7();
  bool tableExists(const QString& tableName) const;
  bool enableForeignKeys();
  bool validateYearMonth(int year, int month) const;
  bool validateSnapshot(int year, int month, const MonthSnapshot& snapshot);
  QVector<int> fullMonthDays(int year, int month) const;
  QVector<int> normalizeDays(int year, int month,
                             const QVector<int>& days) const;
  int daysInMonth(int year, int month) const;
  void setError(const QString& error);
};
