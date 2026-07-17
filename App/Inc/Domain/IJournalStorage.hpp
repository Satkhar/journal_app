#pragma once

#include <QVector>

#include <optional>
#include <vector>

#include "JournalModels.hpp"

class IJournalStorage
{
public:
  virtual ~IJournalStorage() = default;

  virtual QString lastError() const = 0;
  virtual MonthStateResult getMonthState(int year, int month) = 0;
  virtual std::vector<Participant> getParticipantsForMonth(int year,
                                                           int month) = 0;
  // Возвращает effective-настройку: валидные уникальные даты по возрастанию.
  // Если явная настройка отсутствует, storage возвращает все даты месяца.
  virtual QVector<int> getActiveDays(int year, int month) = 0;
  virtual bool saveActiveDays(int year, int month,
                              const QVector<int>& days) = 0;
  virtual std::vector<AttendanceRecord> getMonth(int year, int month) = 0;
  virtual bool saveAttendance(int year, int month,
                              const std::vector<AttendanceRecord>& data) = 0;
  virtual std::vector<ParticipantDayMarker> getDayMarkers(int year,
                                                          int month) = 0;
  virtual bool saveDayMarker(int year, int month,
                             const ParticipantDayMarker& marker) = 0;
  virtual bool removeDayMarker(int year, int month,
                               const ParticipantId& participantId,
                               int day) = 0;
  virtual bool addParticipantToMonth(int year, int month,
                                     const Participant& participant) = 0;
  virtual bool removeParticipantFromMonth(int year, int month,
                                          const ParticipantId& id) = 0;
  // Атомарно заменяет состав, activeDays, attendance и dayMarkers месяца.
  // Данные вне activeDays допустимы: скрытые отметки должны сохраняться.
  virtual bool replaceMonth(int year, int month,
                            const MonthSnapshot& snapshot) = 0;

  virtual std::optional<ParticipantProfile>
  getParticipantProfile(const ParticipantId& id) = 0;
  virtual std::optional<std::vector<ParticipantProfile>>
  listParticipantProfiles(bool includeArchived) = 0;
  virtual bool updateParticipantProfile(const ParticipantProfile& profile) = 0;
  virtual bool setParticipantArchived(const ParticipantId& id,
                                      bool archived) = 0;
};
