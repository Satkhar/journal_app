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
    static_cast<int>(DayMarkerKind::Other);

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

bool ParticipantProfile::isValid() const
{
  const QString trimmedName = displayName.trimmed();
  return id.isValid() && !trimmedName.isEmpty() &&
         trimmedName.size() <= kMaxDisplayNameLength &&
         notes.size() <= kMaxNotesLength &&
         (!birthday.has_value() || birthday->isValid());
}

int CountCheckedActiveDays(const QVector<int>& activeDays,
                           const QHash<int, bool>& attendanceByDay)
{
  return static_cast<int>(std::count_if(
      activeDays.cbegin(), activeDays.cend(),
      [&attendanceByDay](int day)
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
