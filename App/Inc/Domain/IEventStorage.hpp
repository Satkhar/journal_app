#pragma once

#include <optional>
#include <vector>

#include "EventModels.hpp"

class IEventStorage
{
public:
  virtual ~IEventStorage() = default;

  virtual QString lastError() const = 0;
  virtual std::optional<std::vector<EventRecord>> listEvents() = 0;
  virtual std::optional<EventRecord> getEvent(const EventId& id) = 0;
  virtual bool saveEvent(const EventRecord& event) = 0;
  virtual bool removeEvent(const EventId& id, qint64 expectedRevision) = 0;
};
