#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QString>

#include "IJournalStorage.hpp"

class JournalRemote : public IJournalStorage
{
public:
  explicit JournalRemote(const QString& baseUrl, int timeoutMs = 5000);

  bool connect(QString* errorMessage = nullptr);
  QString lastError() const override;
  MonthStateResult getMonthState(int year, int month) override;
  bool getMonthSnapshot(int year, int month, MonthSnapshot* snapshot);

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
  QString baseUrl_;
  int timeoutMs_;
  QString lastError_;
  QNetworkAccessManager network_;

  bool ensureSchema(QString* errorMessage = nullptr);
  int daysInMonth(int year, int month) const;
  QVector<int> fullMonthDays(int year, int month) const;
  bool executePipeline(const QList<QString>& sqlStatements,
                       QJsonArray* outResults, QString* errorMessage = nullptr);
  static QString sqlQuote(const QString& value);
  static QString cellString(const QJsonArray& row, int index);
  static std::optional<int> cellOptionalInt(const QJsonArray& row, int index);
  static std::optional<ParticipantProfile>
  profileFromRow(const QJsonArray& row);
};
