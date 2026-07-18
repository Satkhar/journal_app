#include "JournalModels.hpp"

#include <QDate>

#include <algorithm>
#include <bitset>
#include <limits>

namespace
{

constexpr int kMaxDisplayNameLength = 200;
constexpr int kMaxNotesLength = 4096;
constexpr int kKnownDayMarkerKindsMask =
    static_cast<int>(DayMarkerKind::Payment) |
    static_cast<int>(DayMarkerKind::SpecialTraining) |
    static_cast<int>(DayMarkerKind::FirstVisit) |
    static_cast<int>(DayMarkerKind::Other) |
    static_cast<int>(DayMarkerKind::LedTraining);

} // namespace

bool Birthday::isValid() const
{
  if (year.has_value())
  {
    return QDate(*year, month, day).isValid();
  }

  // Leap year 2000 validates month/day while preserving 29 February.
  return QDate(2000, month, day).isValid();
}

const std::vector<ParticipantRank>& ParticipantRanksInDisplayOrder()
{
  static const std::vector<ParticipantRank> ranks = {
      ParticipantRank::Page,   ParticipantRank::Squire,
      ParticipantRank::Novice, ParticipantRank::Recruit,
      ParticipantRank::Guest,  ParticipantRank::Knight};
  return ranks;
}

QString ParticipantRankStorageValue(ParticipantRank rank)
{
  switch (rank)
  {
  case ParticipantRank::Page:
    return "page";
  case ParticipantRank::Squire:
    return "squire";
  case ParticipantRank::Novice:
    return "novice";
  case ParticipantRank::Recruit:
    return "recruit";
  case ParticipantRank::Guest:
    return "guest";
  case ParticipantRank::Knight:
    return "knight";
  }
  return {};
}

QString ParticipantRankDisplayName(ParticipantRank rank)
{
  switch (rank)
  {
  case ParticipantRank::Page:
    return "Паж";
  case ParticipantRank::Squire:
    return "Оруженосец";
  case ParticipantRank::Novice:
    return "Новичок";
  case ParticipantRank::Recruit:
    return "Рекрут";
  case ParticipantRank::Guest:
    return "Гость";
  case ParticipantRank::Knight:
    return "Рыцарь";
  }
  return {};
}

std::optional<ParticipantRank>
ParticipantRankFromStorageValue(const QString& value)
{
  for (ParticipantRank rank : ParticipantRanksInDisplayOrder())
  {
    if (ParticipantRankStorageValue(rank) == value)
    {
      return rank;
    }
  }
  return std::nullopt;
}

int ParticipantRankSortKey(ParticipantRank rank)
{
  const auto& ranks = ParticipantRanksInDisplayOrder();
  const auto found = std::find(ranks.cbegin(), ranks.cend(), rank);
  return found == ranks.cend()
             ? static_cast<int>(ranks.size())
             : static_cast<int>(std::distance(ranks.cbegin(), found));
}

bool ParticipantProfile::isValid() const
{
  const QString trimmedName = displayName.trimmed();
  return id.isValid() && !trimmedName.isEmpty() &&
         trimmedName.size() <= kMaxDisplayNameLength &&
         fullName.size() <= kMaxParticipantFullNameLength &&
         !fullName.contains('\n') && !fullName.contains('\r') &&
         contact.size() <= kMaxParticipantContactLength &&
         !contact.contains('\n') && !contact.contains('\r') &&
         !ParticipantRankStorageValue(rank).isEmpty() &&
         notes.size() <= kMaxNotesLength &&
         (!birthday.has_value() || birthday->isValid());
}

int CountCheckedActiveDays(const QVector<int>& activeDays,
                           const QHash<int, bool>& attendanceByDay)
{
  return static_cast<int>(std::count_if(
      activeDays.cbegin(), activeDays.cend(), [&attendanceByDay](int day)
      { return attendanceByDay.value(day, false); }));
}

bool IsValidDayMarkerKinds(DayMarkerKinds kinds)
{
  const int value = kinds.toInt();
  return value != 0 && (value & ~kKnownDayMarkerKindsMask) == 0;
}

std::optional<DayMarkerKinds> DayMarkerKindsFromInt(int value)
{
  if (value == 0 || (value & ~kKnownDayMarkerKindsMask) != 0)
  {
    return std::nullopt;
  }
  return DayMarkerKinds(static_cast<DayMarkerKind>(value));
}

int CountDayMarkerKinds(DayMarkerKinds kinds)
{
  return static_cast<int>(
      std::bitset<std::numeric_limits<unsigned int>::digits>(
          static_cast<unsigned int>(kinds.toInt()))
          .count());
}
