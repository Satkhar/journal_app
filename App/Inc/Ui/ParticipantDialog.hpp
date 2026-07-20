#pragma once

#include <QDialog>

#include <optional>

#include "JournalModels.hpp"

struct ParticipantStatisticsData;
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
    ToggleArchive,
    OpenMonth
  };

  explicit ParticipantDialog(
      const ParticipantProfile& profile,
      const ParticipantStatisticsData& statistics,
      bool editable, QWidget* parent = nullptr);
  explicit ParticipantDialog(const ParticipantProfile& profile, bool editable,
                             QWidget* parent = nullptr);

  Action action() const;
  ParticipantProfile profile() const;
  bool targetArchived() const;
  std::optional<JournalMonth> selectedMonth() const;

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
  QCheckBox* trainingStartCheck_;
  QComboBox* trainingStartMonthCombo_;
  QSpinBox* trainingStartYearSpin_;
  QComboBox* rankCombo_;
  QComboBox* combatHandCombo_;
  QTextEdit* notesEdit_;
  QPushButton* saveButton_;
  QPushButton* archiveButton_;
  std::optional<QDate> firstRecordedVisit_;
  bool dirty_;
  std::optional<JournalMonth> selectedMonth_;

  void updateBirthdayControls();
  void updateTrainingStartControls();
  void save();
  void openMonth(int year, int month);
};
