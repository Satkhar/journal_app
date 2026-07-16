#include "JournalModels.hpp"

#include <QDate>

#include <algorithm>

namespace
{

constexpr int kMaxDisplayNameLength = 200;
constexpr int kMaxNotesLength = 4096;

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
