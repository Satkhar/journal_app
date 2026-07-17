#pragma once

#include <memory>
#include <optional>

#include "IJournalStorage.hpp"

struct CopyUsersResult
{
  bool ok;
  int copied;
  int skipped;
  QString errorMessage;
};

enum class CopyScheduleMode
{
  KeepTargetDates,
  ApplySourceWeekdays,
};

class JournalApp
{
public:
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage);

  MonthStateResult getMonthState(int year, int month);
  MonthSnapshot loadMonth(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  CopyUsersResult copyUsersFromMonth(int fromYear, int fromMonth, int toYear,
                                     int toMonth,
                                     CopyScheduleMode scheduleMode);
  bool addUser(const QString& name);
  bool removeParticipant(const ParticipantId& id);
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data);
  bool saveDayMarker(int year, int month,
                     const ParticipantDayMarker& marker);
  bool removeDayMarker(int year, int month,
                       const ParticipantId& participantId, int day);

  std::optional<ParticipantProfile> participantProfile(const ParticipantId& id);
  std::optional<std::vector<ParticipantProfile>>
  participantProfiles(bool includeArchived);
  bool updateParticipantProfile(const ParticipantProfile& profile);
  bool archiveParticipant(const ParticipantId& id);
  bool restoreParticipant(const ParticipantId& id);

private:
  std::unique_ptr<IJournalStorage> storage_;
  int currentYear_;
  int currentMonth_;
};
