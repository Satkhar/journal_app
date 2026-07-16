#include "MonthDaysDialog.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QDate>
#include <QMetaObject>
#include <QPushButton>

#include <iostream>

namespace
{

bool Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
  }
  return condition;
}

bool WeekdayClickSelectsAndClearsGroup()
{
  MonthDaysDialog dialog(2026, 7, {6, 7});
  auto* monday = dialog.findChild<QCheckBox*>("weekdayCheckBox1");
  auto* tuesday = dialog.findChild<QCheckBox*>("weekdayCheckBox2");
  if (!Check(monday != nullptr && tuesday != nullptr,
             "weekday controls missing"))
  {
    return false;
  }
  if (!Check(monday->checkState() == Qt::PartiallyChecked,
             "partial Monday state missing"))
  {
    return false;
  }

  monday->click();
  const QVector<int> allMondays{6, 7, 13, 20, 27};
  if (!Check(dialog.selectedDays() == allMondays,
             "Monday click did not complete weekday"))
  {
    return false;
  }
  if (!Check(monday->checkState() == Qt::Checked &&
                 tuesday->checkState() == Qt::PartiallyChecked,
             "weekday states not synchronized after select"))
  {
    return false;
  }

  monday->click();
  return Check(dialog.selectedDays() == QVector<int>({7}),
               "second Monday click did not clear weekday") &&
         Check(monday->checkState() == Qt::Unchecked,
               "Monday state not cleared");
}

bool BulkControlsStaySynchronized()
{
  MonthDaysDialog dialog(2026, 7, {6});
  auto* selectAll = dialog.findChild<QPushButton*>("selectAllDaysButton");
  auto* clearAll = dialog.findChild<QPushButton*>("clearAllDaysButton");
  auto* wednesday = dialog.findChild<QCheckBox*>("weekdayCheckBox3");
  auto* calendar = dialog.findChild<QCalendarWidget*>();
  if (!Check(selectAll != nullptr && clearAll != nullptr &&
                 wednesday != nullptr && calendar != nullptr,
             "bulk controls missing"))
  {
    return false;
  }

  selectAll->click();
  if (!Check(dialog.selectedDays().size() == 31,
             "select all did not select full month"))
  {
    return false;
  }

  wednesday->click();
  const QVector<int> wednesdays{1, 8, 15, 22, 29};
  for (int day : wednesdays)
  {
    if (!Check(!dialog.selectedDays().contains(day),
               "weekday clear left selected date"))
    {
      return false;
    }
  }
  if (!Check(dialog.selectedDays().size() == 26,
             "weekday clear changed other dates"))
  {
    return false;
  }

  clearAll->click();
  if (!Check(dialog.selectedDays().isEmpty(),
             "clear all left selected dates"))
  {
    return false;
  }
  if (!Check(QMetaObject::invokeMethod(
                 calendar, "clicked", Qt::DirectConnection,
                 Q_ARG(QDate, QDate(2026, 7, 6))),
             "calendar click signal invocation failed"))
  {
    return false;
  }
  auto* monday = dialog.findChild<QCheckBox*>("weekdayCheckBox1");
  return Check(dialog.selectedDays() == QVector<int>({6}),
               "individual date click not applied") &&
         Check(monday != nullptr &&
                   monday->checkState() == Qt::PartiallyChecked,
               "partial state not restored after date click");
}

} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  if (!WeekdayClickSelectsAndClearsGroup() ||
      !BulkControlsStaySynchronized())
  {
    return 1;
  }
  return 0;
}
