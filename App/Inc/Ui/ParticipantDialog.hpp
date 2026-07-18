#pragma once

#include <QDialog>

#include "JournalModels.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;

class ParticipantDialog : public QDialog
{
  Q_OBJECT

public:
  enum class Action
  {
    Cancel,
    Save,
    ToggleArchive
  };

  explicit ParticipantDialog(const ParticipantProfile& profile, bool editable,
                             QWidget* parent = nullptr);

  Action action() const;
  ParticipantProfile profile() const;
  bool targetArchived() const;

private:
  ParticipantProfile original_;
  Action action_;
  QLineEdit* nameEdit_;
  QLineEdit* fullNameEdit_;
  QLineEdit* contactEdit_;
  QCheckBox* birthdayCheck_;
  QSpinBox* daySpin_;
  QSpinBox* monthSpin_;
  QSpinBox* yearSpin_;
  QComboBox* rankCombo_;
  QTextEdit* notesEdit_;
  QPushButton* saveButton_;
  QPushButton* archiveButton_;
  bool dirty_;

  void updateBirthdayControls();
  void save();
};
