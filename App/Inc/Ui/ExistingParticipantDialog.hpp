#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "JournalModels.hpp"

class QComboBox;

class ExistingParticipantDialog : public QDialog
{
public:
  explicit ExistingParticipantDialog(
      std::vector<ParticipantProfile> profiles, QWidget* parent = nullptr);

  std::optional<ParticipantId> selectedParticipantId() const;

private:
  QComboBox* participantCombo_;
};
