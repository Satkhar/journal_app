#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "EventModels.hpp"

class QComboBox;
class QDateEdit;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTextEdit;

class EventDialog : public QDialog
{
public:
  EventDialog(const EventRecord& event,
              const std::vector<ParticipantProfile>& profiles,
              QWidget* parent = nullptr);

  EventRecord eventRecord() const;

private:
  EventRecord original_;
  std::vector<EventParticipantSnapshot> participantChoices_;
  QLineEdit* titleEdit_;
  QDateEdit* dateEdit_;
  QComboBox* categoryCombo_;
  QListWidget* participantsList_;
  QListWidget* attendeesList_;
  QTableWidget* boutsTable_;
  QTextEdit* notesEdit_;

  QComboBox* createSideCombo(const BoutSideRef& side);
  BoutSideRef sideFromCombo(const QComboBox* combo) const;
  std::optional<EventParticipantSnapshot>
  snapshotFromCombo(const QComboBox* combo) const;
  void addBoutRow(const std::optional<EventBout>& bout = std::nullopt);
  void removeBoutButtonClicked(QWidget* button);
  void scheduleBoutTableLayout();
  void updateParticipantRequirements();
  void save();
};
