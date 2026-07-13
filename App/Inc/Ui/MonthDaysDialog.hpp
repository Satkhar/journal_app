#pragma once

#include <QDialog>
#include <QSet>
#include <QVector>

class QCalendarWidget;
class QDialogButtonBox;

class MonthDaysDialog : public QDialog {
 public:
  MonthDaysDialog(int year, int month, const QVector<int>& activeDays,
                  QWidget* parent = nullptr);

  QVector<int> selectedDays() const;

 private:
  void updateCalendarFormat();

  int year_;
  int month_;
  QSet<int> selectedDays_;
  QCalendarWidget* calendar_;
  QDialogButtonBox* buttons_;
};
