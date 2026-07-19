#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;

#include <vector>

#include "JournalModels.hpp"

class CopyUsersDialog : public QDialog
{
public:
  explicit CopyUsersDialog(int targetYear, int targetMonth,
                           std::vector<JournalMonth> sourceMonths,
                           QWidget* parent = nullptr,
                           bool copyWeekdayPatternByDefault = false);

  int sourceYear() const;
  int sourceMonth() const;
  bool copyWeekdayPattern() const;

private:
  QComboBox* sourceMonthCombo_;
  QCheckBox* copyWeekdayPatternCheckBox_;
};
