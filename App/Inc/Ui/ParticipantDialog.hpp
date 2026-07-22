#pragma once

#include <QDialog>

#include <optional>

#include "JournalModels.hpp"

struct ParticipantStatisticsData;
class JournalApp;
class ParticipantEmblemWidget;
class QCheckBox;
class QComboBox;
class QDateEdit;
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
  ParticipantDialog(
      const ParticipantProfile& profile,
      const ParticipantStatisticsData& statistics,
      const std::optional<ParticipantEmblem>& emblem, JournalApp* journalApp,
      bool editable, QWidget* parent = nullptr);
  explicit ParticipantDialog(const ParticipantProfile& profile, bool editable,
                             QWidget* parent = nullptr);

  Action action() const;
  ParticipantProfile profile() const;
  ParticipantCardUpdate cardUpdate() const;
  bool targetArchived() const;
  std::optional<JournalMonth> selectedMonth() const;

private:
  struct RankHistoryControls
  {
    ParticipantRank rank;
    QCheckBox* receivedCheck;
    QCheckBox* dateKnownCheck;
    QDateEdit* dateEdit;
  };

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
  QCheckBox* joinedClubCheck_;
  QDateEdit* joinedClubDateEdit_;
  std::vector<RankHistoryControls> rankHistoryControls_;
  QComboBox* rankCombo_;
  QComboBox* combatHandCombo_;
  ParticipantEmblemWidget* emblemWidget_;
  QTextEdit* notesEdit_;
  QPushButton* saveButton_;
  QPushButton* archiveButton_;
  std::optional<QDate> firstRecordedVisit_;
  bool dirty_;
  std::optional<JournalMonth> selectedMonth_;
  JournalApp* journalApp_;

  void updateBirthdayControls();
  void updateTrainingStartControls();
  void updateMembershipControls();
  void save();
  void openMonth(int year, int month);
};
