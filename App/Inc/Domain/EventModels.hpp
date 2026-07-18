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

struct EventRecord
{
  EventId id;
  QString title;
  QDate date;
  qint64 revision = 0;
  QString notes;
  std::vector<EventParticipantSnapshot> participants;
  std::vector<EventBout> bouts;

  bool isValid() const;
};

EventId CreateEventId();
BoutId CreateBoutId();
