#pragma once

#include <QHashFunctions>
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

struct MonthSnapshot
{
  std::vector<Participant> participants;
  QVector<int> activeDays;
  std::vector<AttendanceRecord> attendance;
};
