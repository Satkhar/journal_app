#pragma once

#include <QDialog>
#include <QSet>
#include <QVector>

#include <array>

class QCalendarWidget;
class QCheckBox;
class QDialogButtonBox;

class MonthDaysDialog : public QDialog {
 public:
  MonthDaysDialog(int year, int month, const QVector<int>& activeDays,
                  QWidget* parent = nullptr);

  QVector<int> selectedDays() const;

 private:
  void toggleWeekday(int dayOfWeek);
  void updateCalendarFormat();
  void updateWeekdayControls();

  int year_;
  int month_;
  QSet<int> selectedDays_;
  QCalendarWidget* calendar_;
  QDialogButtonBox* buttons_;
  std::array<QCheckBox*, 7> weekdayCheckBoxes_;
};
