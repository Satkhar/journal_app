#pragma once

#include <QDate>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <vector>

#include "JournalModels.hpp"

constexpr int kMaxEventTitleLength = 200;
constexpr int kMaxEventNotesLength = 32768;
constexpr int kMaxEventParticipantNameLength = 200;
constexpr int kMaxEventParticipantSnapshotNameLength =
    kMaxParticipantDisplayNameLength;

struct EventId
{
  QString value;

  bool isValid() const;
};

struct BoutId
{
  QString value;

  bool isValid() const;
};

struct EventParticipantSnapshot
{
  ParticipantId participantId;
  QString displayNameSnapshot;
  QString fullNameSnapshot;

  bool isValid() const;
};

struct BoutSideRef
{
  std::optional<ParticipantId> participantId;
  QString freeName;

  bool isValid() const;
};

struct EventBout
{
  BoutId id;
  BoutSideRef sideA;
  BoutSideRef sideB;
  int scoreA = 0;
  int scoreB = 0;

  bool isValid() const;
};

enum class EventCategory : int
{
  Unspecified = 0,
  ClubTrainingTournament = 1,
  ExternalCompetition = 2,
  SoftCombatTournament = 3
};

bool IsValidEventCategory(EventCategory category);
bool IsClassifiedEventCategory(EventCategory category);

struct EventRecord
{
  EventId id;
  QString title;
  QDate date;
  EventCategory category = EventCategory::Unspecified;
  qint64 revision = 0;
  QString notes;
  std::vector<EventParticipantSnapshot> participants;
  // Члены клубной делегации, которые ездили на событие, но не соревновались.
  std::vector<EventParticipantSnapshot> nonCompetingAttendees;
  std::vector<EventBout> bouts;

  bool isValid() const;
};

struct ParticipantEventStatistics
{
  ParticipantId participantId;
  int tournamentCount = 0;
  int boutCount = 0;
  std::optional<QDate> firstTournament;
  std::optional<QDate> lastTournament;
  int clubTournamentCount = 0;
  int externalCompetitionCount = 0;
  int softCombatTournamentCount = 0;
  int unspecifiedTournamentCount = 0;
  int nonCompetingTripCount = 0;
};

EventId CreateEventId();
BoutId CreateBoutId();
