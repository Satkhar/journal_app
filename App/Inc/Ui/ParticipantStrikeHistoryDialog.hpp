#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "JournalModels.hpp"

class JournalApp;
class QComboBox;
class QDateTimeEdit;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

class TimedStrikeTestDialog : public QDialog
{
  Q_OBJECT

public:
  explicit TimedStrikeTestDialog(const TimedStrikeTest& test,
                                 QWidget* parent = nullptr);
  TimedStrikeTest test() const;

private:
  TimedStrikeTest original_;
  QDateTimeEdit* performedAtEdit_;
  QComboBox* handCombo_;
  QSpinBox* strikeCountSpin_;
  QSpinBox* durationSpin_;
  QComboBox* weaponCombo_;
  QTextEdit* noteEdit_;

  void acceptIfValid();
};
class ParticipantStrikeHistoryDialog : public QDialog
{
  Q_OBJECT

public:
  ParticipantStrikeHistoryDialog(JournalApp& app,
                                 const ParticipantProfile& profile,
                                 bool editable, QWidget* parent = nullptr);

private:
  JournalApp& app_;
  ParticipantProfile profile_;
  bool editable_;
  std::vector<TimedStrikeTest> tests_;
  QLabel* summary_;
  QTableWidget* table_;
  QPushButton* addButton_;
  QPushButton* editButton_;
  QPushButton* removeButton_;

  void reload();
  void updateSummary();
  std::optional<TimedStrikeTest> selectedTest() const;
  void addTest();
  void editTest();
  void removeTest();
};
