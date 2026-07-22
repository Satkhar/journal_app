#pragma once

#include <memory>
#include <optional>

#include "IJournalStorage.hpp"

struct AddParticipantsResult
{
  bool ok;
  int copied;
  int skipped;
  QString errorMessage;
  MonthSnapshot snapshot;
};

enum class CopyScheduleMode
{
  KeepTargetDates,
  ApplySourceWeekdays,
};

class JournalApp
{
public:
  // JournalApp единолично владеет storage. Все команды получают месяц явно,
  // поэтому будущий async callback не сможет записать в "последний" месяц.
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage);

  QString lastError() const;
  std::optional<std::vector<JournalMonth>> configuredMonths();
  MonthStateResult getMonthState(int year, int month);
  MonthSnapshot loadMonth(int year, int month);
  bool saveMonthSnapshot(int year, int month,
                         const MonthSnapshot& snapshot);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  AddParticipantsResult prepareParticipantsFromMonth(
      int fromYear, int fromMonth, int toYear, int toMonth,
      CopyScheduleMode scheduleMode);
  bool addUser(int year, int month, const QString& fullName);
  bool addExistingParticipant(int year, int month,
                              const ParticipantId& id);
  bool removeParticipant(int year, int month, const ParticipantId& id);
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data);
  bool saveDayMarker(int year, int month,
                     const ParticipantDayMarker& marker);
  bool removeDayMarker(int year, int month,
                       const ParticipantId& participantId, int day);

  std::optional<ParticipantProfile> participantProfile(const ParticipantId& id);
  std::optional<ParticipantJournalStatistics>
  participantStatistics(const ParticipantId& id);
  std::optional<ParticipantEmblem> participantEmblem(const ParticipantId& id);
  bool updateParticipantCard(const ParticipantCardUpdate& update);
  std::optional<std::vector<TimedStrikeTest>>
  timedStrikeTests(const ParticipantId& id);
  bool saveTimedStrikeTest(const TimedStrikeTest& test);
  bool removeTimedStrikeTest(const TimedStrikeTestId& id,
                             qint64 expectedRevision);
  std::optional<std::vector<ParticipantProfile>>
  participantProfiles(bool includeArchived);
  bool updateParticipantProfile(const ParticipantProfile& profile);
  bool archiveParticipant(const ParticipantId& id);
  bool restoreParticipant(const ParticipantId& id);

private:
  std::unique_ptr<IJournalStorage> storage_;
};
