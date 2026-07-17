#include "AttendanceCellWidget.hpp"
#include "DayMarkerDialog.hpp"
#include "MonthDaysDialog.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QDate>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QToolButton>

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

bool DayMarkerDialogSupportsMultipleKindsAndClear()
{
  DayMarkerDialog dialog(
      "Alice", QDate(2026, 7, 16),
      DayMarkerKind::Payment | DayMarkerKind::SpecialTraining, "Сбор");
  auto* payment =
      dialog.findChild<QCheckBox*>("paymentMarkerCheckBox");
  auto* special =
      dialog.findChild<QCheckBox*>("specialTrainingMarkerCheckBox");
  auto* firstVisit =
      dialog.findChild<QCheckBox*>("firstVisitMarkerCheckBox");
  auto* save = dialog.findChild<QPushButton*>("saveDayMarkerButton");
  if (!Check(payment && special && firstVisit && save,
             "day marker dialog controls missing"))
  {
    return false;
  }
  firstVisit->setChecked(true);
  save->click();
  if (!Check(dialog.result() == QDialog::Accepted &&
                 dialog.selectedKinds().testFlag(DayMarkerKind::Payment) &&
                 dialog.selectedKinds().testFlag(
                     DayMarkerKind::SpecialTraining) &&
                 dialog.selectedKinds().testFlag(DayMarkerKind::FirstVisit) &&
                 dialog.note() == "Сбор" && !dialog.clearRequested(),
             "multiple marker kinds were not saved"))
  {
    return false;
  }

  DayMarkerDialog clearDialog("Alice", QDate(2026, 7, 16),
                              DayMarkerKind::Payment, "Оплата");
  auto* clear =
      clearDialog.findChild<QPushButton*>("clearDayMarkerButton");
  if (!Check(clear != nullptr, "clear marker button missing"))
  {
    return false;
  }
  clear->click();
  return Check(clearDialog.result() == QDialog::Accepted &&
                   clearDialog.clearRequested(),
               "clear marker action was not accepted");
}

bool NoteOnlyMarkerBecomesOther()
{
  DayMarkerDialog dialog("Alice", QDate(2026, 7, 16), DayMarkerKinds(), "");
  auto* note = dialog.findChild<QLineEdit*>("dayMarkerNoteEdit");
  auto* save = dialog.findChild<QPushButton*>("saveDayMarkerButton");
  if (!Check(note && save, "note-only marker controls missing"))
  {
    return false;
  }
  note->setText("Пробная заметка");
  save->click();
  return Check(
      dialog.selectedKinds().testFlag(DayMarkerKind::Other) &&
          CountDayMarkerKinds(dialog.selectedKinds()) == 1,
      "note-only marker did not select Other");
}

bool AttendanceCellUsesCompactSemanticBadge()
{
  const ParticipantId id{"12345678-1234-1234-1234-123456789abc"};
  const ParticipantDayMarker marker{
      id, 16, DayMarkerKind::Payment | DayMarkerKind::FirstVisit,
      "<оплачено>"};
  AttendanceCellWidget cell(false, "Alice", QDate(2026, 7, 16), marker);
  auto* checkBox = cell.attendanceCheckBox();
  auto* markerButton = cell.markerButton();
  if (!Check(checkBox && markerButton, "attendance cell controls missing"))
  {
    return false;
  }
  markerButton->click();
  if (!Check(!checkBox->isChecked(),
             "marker click changed attendance state") ||
      !Check(markerButton->height() <= 22 && markerButton->text() == "2",
             "marker badge is not compact or does not show kind count") ||
      !Check(markerButton->toolTip().contains("Оплата") &&
                 markerButton->toolTip().contains("Первое посещение") &&
                 markerButton->toolTip().contains("&lt;оплачено&gt;"),
             "marker tooltip lost semantics or escaping"))
  {
    return false;
  }
  cell.setEditable(false);
  return Check(!checkBox->isEnabled() && !markerButton->isEnabled(),
               "read-only marker cell remains editable");
}

} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  if (!WeekdayClickSelectsAndClearsGroup() ||
      !BulkControlsStaySynchronized() ||
      !DayMarkerDialogSupportsMultipleKindsAndClear() ||
      !NoteOnlyMarkerBecomesOther() ||
      !AttendanceCellUsesCompactSemanticBadge())
  {
    return 1;
  }
  return 0;
}
