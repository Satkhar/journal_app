#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "IEventStorage.hpp"

class EventApp
{
public:
  explicit EventApp(std::unique_ptr<IEventStorage> storage);

  QString lastError() const;
  std::optional<std::vector<EventRecord>> events();
  std::optional<EventRecord> event(const EventId& id);
  bool save(const EventRecord& event);
  bool remove(const EventId& id, qint64 expectedRevision);

private:
  std::unique_ptr<IEventStorage> storage_;
};
