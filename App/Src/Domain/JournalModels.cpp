#include "JournalModels.hpp"

#include <QCryptographicHash>
#include <QDate>

#include <algorithm>
#include <bitset>
#include <limits>
#include <set>

namespace
{

constexpr int kMaxNotesLength = 4096;
constexpr int kKnownDayMarkerKindsMask =
    static_cast<int>(DayMarkerKind::Payment) |
    static_cast<int>(DayMarkerKind::SpecialTraining) |
    static_cast<int>(DayMarkerKind::FirstVisit) |
    static_cast<int>(DayMarkerKind::Other) |
    static_cast<int>(DayMarkerKind::LedTraining);

bool isStructurallyValidTrainingStartMonth(
    const std::optional<JournalMonth>& month)
{
  if (!month.has_value())
  {
    return true;
  }
  const QDate start(month->year, month->month, 1);
  return month->year >= 1900 && start.isValid();
}

} // namespace

bool IsTrainingStartMonthNotAfter(
    const std::optional<JournalMonth>& month, const QDate& referenceDate)
{
  if (!isStructurallyValidTrainingStartMonth(month) ||
      !referenceDate.isValid())
  {
    return false;
  }
  if (!month.has_value())
  {
    return true;
  }
  const QDate referenceMonth(referenceDate.year(), referenceDate.month(), 1);
  return QDate(month->year, month->month, 1) <= referenceMonth;
}

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
      ParticipantRank::Novice, ParticipantRank::Recruit,
      ParticipantRank::Page, ParticipantRank::Squire,
      ParticipantRank::Knight, ParticipantRank::Guest};
  return ranks;
}

const std::vector<ParticipantRank>& ParticipantRanksWithHistory()
{
  // Guest — статус посетителя, а не получаемое клубное звание.
  static const std::vector<ParticipantRank> ranks = {
      ParticipantRank::Novice, ParticipantRank::Recruit,
      ParticipantRank::Page, ParticipantRank::Squire,
      ParticipantRank::Knight};
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

bool IsParticipantRankWithHistory(ParticipantRank rank)
{
  const auto& ranks = ParticipantRanksWithHistory();
  return std::find(ranks.cbegin(), ranks.cend(), rank) != ranks.cend();
}

bool ParticipantRankHistoryEntry::isValid() const
{
  return IsParticipantRankWithHistory(rank) &&
         (!obtainedOn.has_value() ||
          (obtainedOn->isValid() && obtainedOn->year() >= 1900));
}

QString CombatHandStorageValue(CombatHand hand)
{
  switch (hand)
  {
  case CombatHand::Unknown:
    return "unknown";
  case CombatHand::Right:
    return "right";
  case CombatHand::Left:
    return "left";
  }
  return {};
}

QString CombatHandDisplayName(CombatHand hand)
{
  switch (hand)
  {
  case CombatHand::Unknown:
    return "Не указана";
  case CombatHand::Right:
    return "Правая";
  case CombatHand::Left:
    return "Левая";
  }
  return {};
}

std::optional<CombatHand> CombatHandFromStorageValue(const QString& value)
{
  for (CombatHand hand :
       {CombatHand::Unknown, CombatHand::Right, CombatHand::Left})
  {
    if (CombatHandStorageValue(hand) == value)
    {
      return hand;
    }
  }
  return std::nullopt;
}

bool ParticipantProfile::isValid() const
{
  const QString trimmedDisplayName = displayName.trimmed();
  const QString trimmedHistoricalName = historicalName.trimmed();
  const QString trimmedFullName = fullName.trimmed();
  std::set<ParticipantRank> historyRanks;
  for (const ParticipantRankHistoryEntry& entry : rankHistory)
  {
    if (!entry.isValid() || !historyRanks.insert(entry.rank).second)
    {
      return false;
    }
  }
  return id.isValid() &&
         (!trimmedHistoricalName.isEmpty() || !trimmedFullName.isEmpty()) &&
         !trimmedDisplayName.isEmpty() &&
         trimmedDisplayName == ParticipantDisplayName(*this) &&
         trimmedDisplayName.size() <= kMaxParticipantDisplayNameLength &&
         trimmedHistoricalName.size() <= kMaxParticipantHistoricalNameLength &&
         !historicalName.contains('\n') && !historicalName.contains('\r') &&
         fullName.size() <= kMaxParticipantFullNameLength &&
         !fullName.contains('\n') && !fullName.contains('\r') &&
         contact.size() <= kMaxParticipantContactLength &&
         !contact.contains('\n') && !contact.contains('\r') &&
         !ParticipantRankStorageValue(rank).isEmpty() &&
         !CombatHandStorageValue(combatHand).isEmpty() &&
         notes.size() <= kMaxNotesLength &&
         (!birthday.has_value() || birthday->isValid()) &&
         (!joinedClubOn.has_value() ||
          (joinedClubOn->isValid() && joinedClubOn->year() >= 1900)) &&
         isStructurallyValidTrainingStartMonth(trainingStartMonth);
}

bool AreParticipantMilestoneDatesNotAfter(
    const ParticipantProfile& profile, const QDate& referenceDate)
{
  if (!profile.isValid() || !referenceDate.isValid() ||
      (profile.joinedClubOn.has_value() &&
       *profile.joinedClubOn > referenceDate))
  {
    return false;
  }
  return std::none_of(
      profile.rankHistory.cbegin(), profile.rankHistory.cend(),
      [&referenceDate](const ParticipantRankHistoryEntry& entry)
      {
        return entry.obtainedOn.has_value() &&
               *entry.obtainedOn > referenceDate;
      });
}

bool ParticipantEmblem::isValid() const
{
  static const QByteArray pngSignature =
      QByteArray::fromHex("89504e470d0a1a0a");
  return participantId.isValid() && !imageData.isEmpty() &&
         imageData.size() <= kMaxParticipantEmblemBytes &&
         imageData.startsWith(pngSignature) && sha256.size() == 32 &&
         QCryptographicHash::hash(imageData, QCryptographicHash::Sha256) ==
             sha256 &&
         !originalFileName.trimmed().isEmpty() &&
         originalFileName.size() <= 255 && !originalFileName.contains('\n') &&
         !originalFileName.contains('\r') && pixelWidth > 0 &&
         pixelWidth <= kMaxParticipantEmblemDimension && pixelHeight > 0 &&
         pixelHeight <= kMaxParticipantEmblemDimension && revision >= 0;
}

bool ParticipantCardUpdate::isValid() const
{
  if (!profile.isValid())
  {
    return false;
  }
  switch (emblemAction)
  {
  case ParticipantEmblemAction::Keep:
    return !emblem.has_value() && expectedEmblemRevision == 0;
  case ParticipantEmblemAction::Replace:
    return emblem.has_value() && emblem->isValid() &&
           emblem->participantId == profile.id &&
           expectedEmblemRevision == emblem->revision;
  case ParticipantEmblemAction::Remove:
    return !emblem.has_value() && expectedEmblemRevision >= 1;
  }
  return false;
}

TimedStrikeTestId CreateTimedStrikeTestId()
{
  return {QUuid::createUuid().toString(QUuid::WithoutBraces)};
}

QString StrikeHandStorageValue(StrikeHand hand)
{
  switch (hand)
  {
  case StrikeHand::Right:
    return "right";
  case StrikeHand::Left:
    return "left";
  }
  return {};
}

QString StrikeHandDisplayName(StrikeHand hand)
{
  switch (hand)
  {
  case StrikeHand::Right:
    return "Правая";
  case StrikeHand::Left:
    return "Левая";
  }
  return {};
}

std::optional<StrikeHand> StrikeHandFromStorageValue(const QString& value)
{
  for (StrikeHand hand : {StrikeHand::Right, StrikeHand::Left})
  {
    if (StrikeHandStorageValue(hand) == value)
    {
      return hand;
    }
  }
  return std::nullopt;
}

QString StrikeWeaponStorageValue(StrikeWeapon weapon)
{
  switch (weapon)
  {
  case StrikeWeapon::Sword:
    return "sword";
  case StrikeWeapon::Tyambara:
    return "tyambara";
  }
  return {};
}

QString StrikeWeaponDisplayName(StrikeWeapon weapon)
{
  switch (weapon)
  {
  case StrikeWeapon::Sword:
    return "Меч";
  case StrikeWeapon::Tyambara:
    return "Тямбара";
  }
  return {};
}

std::optional<StrikeWeapon>
StrikeWeaponFromStorageValue(const QString& value)
{
  for (StrikeWeapon weapon : {StrikeWeapon::Sword, StrikeWeapon::Tyambara})
  {
    if (StrikeWeaponStorageValue(weapon) == value)
    {
      return weapon;
    }
  }
  return std::nullopt;
}

bool TimedStrikeTest::isValid() const
{
  const QString canonicalUtc =
      performedAt.toUTC().toString(Qt::ISODateWithMs);
  return id.isValid() && participantId.isValid() && performedAt.isValid() &&
         canonicalUtc.size() == 24 && canonicalUtc.endsWith('Z') &&
         !StrikeHandStorageValue(hand).isEmpty() && strikeCount >= 1 &&
         strikeCount <= 100000 && durationSeconds >= 1 &&
         durationSeconds <= 3600 &&
         !StrikeWeaponStorageValue(weapon).isEmpty() &&
         note.size() <= kMaxTimedStrikeTestNoteLength && revision >= 0;
}

double TimedStrikeTest::strikesPerSecond() const
{
  return durationSeconds > 0
             ? static_cast<double>(strikeCount) / durationSeconds
             : 0.0;
}

double TimedStrikeTest::strikesPerMinute() const
{
  return strikesPerSecond() * 60.0;
}

QString ParticipantDisplayName(const ParticipantProfile& profile)
{
  const QString historicalName = profile.historicalName.trimmed();
  if (!historicalName.isEmpty())
  {
    return historicalName;
  }
  return profile.fullName.trimmed();
}

bool IsValidParticipantSnapshot(const Participant& participant)
{
  ParticipantProfile profile;
  profile.id = participant.id;
  profile.displayName = participant.displayName;
  profile.historicalName = participant.historicalName;
  profile.fullName = participant.fullName;
  return profile.isValid();
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
