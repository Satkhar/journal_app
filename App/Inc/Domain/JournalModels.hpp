#pragma once

#include <QFlags>
#include <QHash>
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
  // displayName вычисляется: historicalName, если задано, иначе fullName.
  QString displayName;
  QString historicalName;
  QString fullName;
};

struct Birthday
{
  int day;
  int month;
  std::optional<int> year;

  bool isValid() const;
};

enum class ParticipantRank
{
  Page,
  Squire,
  Novice,
  Recruit,
  Guest,
  Knight,
};

enum class CombatHand
{
  Unknown,
  Right,
  Left,
};

const std::vector<ParticipantRank>& ParticipantRanksInDisplayOrder();
QString ParticipantRankStorageValue(ParticipantRank rank);
QString ParticipantRankDisplayName(ParticipantRank rank);
std::optional<ParticipantRank>
ParticipantRankFromStorageValue(const QString& value);
int ParticipantRankSortKey(ParticipantRank rank);
QString CombatHandStorageValue(CombatHand hand);
QString CombatHandDisplayName(CombatHand hand);
std::optional<CombatHand> CombatHandFromStorageValue(const QString& value);

struct ParticipantProfile
{
  ParticipantId id;
  // Готовое имя для таблиц и month snapshots. Storage поддерживает его как
  // historicalName, если оно задано, иначе fullName.
  QString displayName;
  QString historicalName;
  QString fullName;
  QString contact;
  std::optional<Birthday> birthday;
  ParticipantRank rank{ParticipantRank::Guest};
  CombatHand combatHand{CombatHand::Unknown};
  QString notes;
  bool archived = false;

  bool isValid() const;
};

constexpr int kMaxParticipantDisplayNameLength = 300;
constexpr int kMaxParticipantHistoricalNameLength = 200;
constexpr int kMaxParticipantFullNameLength = 300;
constexpr int kMaxParticipantContactLength = 500;

QString ParticipantDisplayName(const ParticipantProfile& profile);
bool IsValidParticipantSnapshot(const Participant& participant);

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
  LedTraining = 0x10,
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

// Идентификатор сформированного месяца для навигации и выбора источника.
// Это value object, а не признак существования: наличие подтверждает storage.
struct JournalMonth
{
  int year;
  int month;
};

inline bool operator==(const JournalMonth& lhs, const JournalMonth& rhs)
{
  return lhs.year == rhs.year && lhs.month == rhs.month;
}

struct MonthSnapshot
{
  MonthState state{MonthState::Error};
  QString errorMessage;
  std::vector<Participant> participants;
  QVector<int> activeDays;
  std::vector<AttendanceRecord> attendance;
  std::vector<ParticipantDayMarker> dayMarkers;
};
