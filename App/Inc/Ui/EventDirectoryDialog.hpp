#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "EventApp.hpp"
#include "JournalModels.hpp"

class QTableWidget;

class EventDirectoryDialog : public QDialog
{
public:
  EventDirectoryDialog(EventApp& eventApp,
                       std::vector<ParticipantProfile> profiles,
                       QWidget* parent = nullptr);

private:
  struct EventSelection
  {
    EventId id;
    qint64 revision;
  };

  EventApp& eventApp_;
  std::vector<ParticipantProfile> profiles_;
  QTableWidget* table_;

  void reload();
  std::optional<EventSelection> selectedEvent() const;
  void addEvent();
  void editEvent();
  void removeEvent();
};
