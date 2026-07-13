#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "JournalModels.hpp"

class QTableWidget;

class ParticipantDirectoryDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ParticipantDirectoryDialog(
      const std::vector<ParticipantProfile>& profiles,
      QWidget* parent = nullptr);

  std::optional<ParticipantId> selectedId() const;

private:
  QTableWidget* table_;

  void acceptSelection();
};
