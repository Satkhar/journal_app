#include "EventApp.hpp"

#include <utility>

EventApp::EventApp(std::unique_ptr<IEventStorage> storage)
    : storage_(std::move(storage))
{
}

QString EventApp::lastError() const
{
  return storage_->lastError();
}

std::optional<std::vector<EventRecord>> EventApp::events()
{
  return storage_->listEvents();
}

std::optional<EventRecord> EventApp::event(const EventId& id)
{
  return id.isValid() ? storage_->getEvent(id) : std::nullopt;
}

std::optional<ParticipantEventStatistics>
EventApp::participantStatistics(const ParticipantId& id)
{
  return id.isValid() ? storage_->participantStatistics(id) : std::nullopt;
}

bool EventApp::save(const EventRecord& event)
{
  return event.isValid() && IsClassifiedEventCategory(event.category) &&
         storage_->saveEvent(event);
}

bool EventApp::remove(const EventId& id, qint64 expectedRevision)
{
  return id.isValid() && expectedRevision >= 1 &&
         storage_->removeEvent(id, expectedRevision);
}
