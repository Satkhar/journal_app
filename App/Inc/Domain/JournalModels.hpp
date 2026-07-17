#pragma once

#include <QHash>
#include <QHashFunctions>
#include <QFlags>
#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>
#include <vector>

struct ParticipantId
{
  QString value;

  bool isValid() const
  {
    const QUuid parsed(value);
    return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == value;
  }
};

inline bool operator==(const ParticipantId& lhs, const ParticipantId& rhs)
{
  return lhs.value == rhs.value;
}

inline bool operator!=(const ParticipantId& lhs, const ParticipantId& rhs)
{
  return !(lhs == rhs);
}

inline size_t qHash(const ParticipantId& id, size_t seed = 0) noexcept
{
  return qHash(id.value, seed);
}

struct Participant
{
  ParticipantId id;
  QString displayName;
};

struct Birthday
{
  int day;
  int month;
  std::optional<int> year;

  bool isValid() const;
};

struct ParticipantProfile
{
  ParticipantId id;
  QString displayName;
  std::optional<Birthday> birthday;
  QString notes;
  bool archived = false;

  bool isValid() const;
};

struct AttendanceRecord
{
  ParticipantId participantId;
  int day;
  bool isChecked;
};

enum class DayMarkerKind : quint8
{
  Payment = 0x01,
  SpecialTraining = 0x02,
  FirstVisit = 0x04,
  Other = 0x08,
};

Q_DECLARE_FLAGS(DayMarkerKinds, DayMarkerKind)
Q_DECLARE_OPERATORS_FOR_FLAGS(DayMarkerKinds)

constexpr int kMaxDayMarkerNoteLength = 500;

struct ParticipantDayMarker
{
  ParticipantId participantId;
  int day;
  DayMarkerKinds kinds;
  QString note;
};

bool IsValidDayMarkerKinds(DayMarkerKinds kinds);
std::optional<DayMarkerKinds> DayMarkerKindsFromInt(int value);
int CountDayMarkerKinds(DayMarkerKinds kinds);

int CountCheckedActiveDays(const QVector<int>& activeDays,
                           const QHash<int, bool>& attendanceByDay);

enum class MonthState
{
  Missing,
  Ready,
  Error,
};

struct MonthStateResult
{
  MonthState state{MonthState::Error};
  QString errorMessage;
};

struct MonthSnapshot
{
  MonthState state{MonthState::Error};
  QString errorMessage;
  std::vector<Participant> participants;
  QVector<int> activeDays;
  std::vector<AttendanceRecord> attendance;
  std::vector<ParticipantDayMarker> dayMarkers;
};
