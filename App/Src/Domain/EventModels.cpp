#include "EventModels.hpp"

#include <QSet>
#include <QUuid>

namespace
{

bool IsCanonicalUuid(const QString& value)
{
  const QUuid parsed(value);
  return !parsed.isNull() &&
         parsed.toString(QUuid::WithoutBraces) == value;
}

bool IsSingleLine(const QString& value, int maxLength)
{
  const QString trimmed = value.trimmed();
  return !trimmed.isEmpty() && trimmed.size() <= maxLength &&
         !value.contains('\n') && !value.contains('\r');
}

} // namespace

bool EventId::isValid() const
{
  return IsCanonicalUuid(value);
}

bool BoutId::isValid() const
{
  return IsCanonicalUuid(value);
}

bool EventParticipantSnapshot::isValid() const
{
  return participantId.isValid() &&
         IsSingleLine(displayNameSnapshot, kMaxEventParticipantNameLength) &&
         fullNameSnapshot.size() <= kMaxParticipantFullNameLength &&
         !fullNameSnapshot.contains('\n') && !fullNameSnapshot.contains('\r');
}

bool BoutSideRef::isValid() const
{
  if (participantId.has_value())
  {
    return participantId->isValid() && freeName.isEmpty();
  }
  return IsSingleLine(freeName, kMaxEventParticipantNameLength);
}

bool EventBout::isValid() const
{
  if (!id.isValid() || !sideA.isValid() || !sideB.isValid() || scoreA < 0 ||
      scoreB < 0)
  {
    return false;
  }
  return !sideA.participantId.has_value() ||
         !sideB.participantId.has_value() ||
         *sideA.participantId != *sideB.participantId;
}

bool EventRecord::isValid() const
{
  if (!id.isValid() || !date.isValid() || revision < 0 ||
      !IsSingleLine(title, kMaxEventTitleLength) ||
      notes.size() > kMaxEventNotesLength)
  {
    return false;
  }

  QSet<QString> participantIds;
  for (const EventParticipantSnapshot& participant : participants)
  {
    if (!participant.isValid() ||
        participantIds.contains(participant.participantId.value))
    {
      return false;
    }
    participantIds.insert(participant.participantId.value);
  }

  QSet<QString> boutIds;
  for (const EventBout& bout : bouts)
  {
    if (!bout.isValid() || boutIds.contains(bout.id.value))
    {
      return false;
    }
    boutIds.insert(bout.id.value);
    const auto sideBelongsToRoster = [&participantIds](const BoutSideRef& side)
    {
      return !side.participantId.has_value() ||
             participantIds.contains(side.participantId->value);
    };
    if (!sideBelongsToRoster(bout.sideA) ||
        !sideBelongsToRoster(bout.sideB))
    {
      return false;
    }
  }
  return true;
}

EventId CreateEventId()
{
  return {QUuid::createUuid().toString(QUuid::WithoutBraces)};
}

BoutId CreateBoutId()
{
  return {QUuid::createUuid().toString(QUuid::WithoutBraces)};
}
