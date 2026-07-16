#pragma once

#include <QDialog>

class QCalendarWidget;
class QCheckBox;

class CopyUsersDialog : public QDialog
{
public:
  explicit CopyUsersDialog(int targetYear, int targetMonth,
                           QWidget* parent = nullptr,
                           bool copyWeekdayPatternByDefault = false);

  int sourceYear() const;
  int sourceMonth() const;
  bool copyWeekdayPattern() const;

private:
  QCalendarWidget* calendar_;
  QCheckBox* copyWeekdayPatternCheckBox_;
};
