#pragma once

#include <memory>

#include "IJournalStorage.hpp"
#include "SqliteConnect.hpp"

class JournalLocal : public IJournalStorage
{
public:
  explicit JournalLocal(std::unique_ptr<SqliteConnect> sqlite);

  QString lastError() const override;
  MonthStateResult getMonthState(int year, int month) override;
  std::vector<Participant> getParticipantsForMonth(int year,
                                                   int month) override;
  QVector<int> getActiveDays(int year, int month) override;
  bool saveActiveDays(int year, int month, const QVector<int>& days) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveAttendance(int year, int month,
                      const std::vector<AttendanceRecord>& data) override;
  std::vector<ParticipantDayMarker> getDayMarkers(int year,
                                                  int month) override;
  bool saveDayMarker(int year, int month,
                     const ParticipantDayMarker& marker) override;
  bool removeDayMarker(int year, int month,
                       const ParticipantId& participantId, int day) override;
  bool addParticipantToMonth(int year, int month,
                             const ParticipantProfile& profile) override;
  bool removeParticipantFromMonth(int year, int month,
                                  const ParticipantId& id) override;
  bool replaceMonth(int year, int month,
                    const MonthSnapshot& snapshot) override;
  std::optional<ParticipantProfile>
  getParticipantProfile(const ParticipantId& id) override;
  std::optional<std::vector<ParticipantProfile>>
  listParticipantProfiles(bool includeArchived) override;
  bool updateParticipantProfile(const ParticipantProfile& profile) override;
  bool setParticipantArchived(const ParticipantId& id, bool archived) override;

private:
  std::unique_ptr<SqliteConnect> sqlite_;
};
