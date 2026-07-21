#pragma once

#include <QVector>

#include <optional>
#include <vector>

#include "IMonthSnapshotStore.hpp"
#include "JournalModels.hpp"

class IJournalStorage : public IMonthSnapshotStore
{
public:
  virtual ~IJournalStorage() = default;

  // Точечные операции нужны редактору. Полный перенос месяца выполняется
  // только через IMonthSnapshotStore, чтобы sync не зависел от CRUD API.
  // Сформированные месяцы возвращаются от новых к старым. nullopt означает
  // ошибку чтения, пустой vector — отсутствие сформированных месяцев.
  virtual std::optional<std::vector<JournalMonth>> listMonths() = 0;
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
                                     const ParticipantProfile& profile) = 0;
  virtual bool removeParticipantFromMonth(int year, int month,
                                          const ParticipantId& id) = 0;
  // Профили глобальны и не входят в MonthSnapshot целиком. Contact, birthday,
  // rank, notes и archive status пока остаются локальными данными.
  virtual std::optional<ParticipantProfile>
  getParticipantProfile(const ParticipantId& id) = 0;
  // Вычисляемый read-model: месяцы состава идут хронологически, выключенные
  // даты не считаются посещениями. nullopt означает storage/not-found error,
  // а профиль без истории возвращает валидную статистику с пустым months.
  virtual std::optional<ParticipantJournalStatistics>
  participantStatistics(const ParticipantId& id) = 0;
  // Герб загружается лениво и не входит в лёгкий ParticipantProfile или
  // month snapshot. Missing возвращает nullopt с пустым lastError().
  virtual std::optional<ParticipantEmblem>
  getParticipantEmblem(const ParticipantId& id) = 0;
  virtual bool updateParticipantCard(const ParticipantCardUpdate& update) = 0;
  // Замеры — авторитетная история. Скорость и прогресс вычисляются из неё.
  virtual std::optional<std::vector<TimedStrikeTest>>
  timedStrikeTests(const ParticipantId& id) = 0;
  virtual bool saveTimedStrikeTest(const TimedStrikeTest& test) = 0;
  virtual bool removeTimedStrikeTest(const TimedStrikeTestId& id,
                                     qint64 expectedRevision) = 0;
  virtual std::optional<std::vector<ParticipantProfile>>
  listParticipantProfiles(bool includeArchived) = 0;
  virtual bool updateParticipantProfile(const ParticipantProfile& profile) = 0;
  virtual bool setParticipantArchived(const ParticipantId& id,
                                      bool archived) = 0;
};
