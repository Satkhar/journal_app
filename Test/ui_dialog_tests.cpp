#include "AttendanceCellWidget.hpp"
#include "DayMarkerDialog.hpp"
#include "EventDialog.hpp"
#include "MonthDaysDialog.hpp"
#include "ParticipantDialog.hpp"
#include "ParticipantDirectoryDialog.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
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
  if (!Check(dialog.selectedDays().isEmpty(), "clear all left selected dates"))
  {
    return false;
  }
  if (!Check(QMetaObject::invokeMethod(calendar, "clicked",
                                       Qt::DirectConnection,
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
  auto* payment = dialog.findChild<QCheckBox*>("paymentMarkerCheckBox");
  auto* special = dialog.findChild<QCheckBox*>("specialTrainingMarkerCheckBox");
  auto* firstVisit = dialog.findChild<QCheckBox*>("firstVisitMarkerCheckBox");
  auto* trainer = dialog.findChild<QCheckBox*>("trainerMarkerCheckBox");
  auto* save = dialog.findChild<QPushButton*>("saveDayMarkerButton");
  if (!Check(payment && special && firstVisit && trainer && save,
             "day marker dialog controls missing"))
  {
    return false;
  }
  firstVisit->setChecked(true);
  trainer->setChecked(true);
  save->click();
  if (!Check(
          dialog.result() == QDialog::Accepted &&
              dialog.selectedKinds().testFlag(DayMarkerKind::Payment) &&
              dialog.selectedKinds().testFlag(DayMarkerKind::SpecialTraining) &&
              dialog.selectedKinds().testFlag(DayMarkerKind::FirstVisit) &&
              dialog.selectedKinds().testFlag(DayMarkerKind::LedTraining) &&
              dialog.note() == "Сбор" && !dialog.clearRequested(),
          "multiple marker kinds were not saved"))
  {
    return false;
  }

  DayMarkerDialog clearDialog("Alice", QDate(2026, 7, 16),
                              DayMarkerKind::Payment, "Оплата");
  auto* clear = clearDialog.findChild<QPushButton*>("clearDayMarkerButton");
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
  return Check(dialog.selectedKinds().testFlag(DayMarkerKind::Other) &&
                   CountDayMarkerKinds(dialog.selectedKinds()) == 1,
               "note-only marker did not select Other");
}

bool AttendanceCellUsesCompactSemanticBadge()
{
  const ParticipantId id{"12345678-1234-1234-1234-123456789abc"};
  const ParticipantDayMarker marker{
      id, 16,
      DayMarkerKind::Payment | DayMarkerKind::FirstVisit |
          DayMarkerKind::LedTraining,
      "<оплачено>"};
  AttendanceCellWidget cell(false, "Alice", QDate(2026, 7, 16), marker);
  auto* checkBox = cell.attendanceCheckBox();
  auto* markerButton = cell.markerButton();
  if (!Check(checkBox && markerButton, "attendance cell controls missing"))
  {
    return false;
  }
  markerButton->click();
  if (!Check(!checkBox->isChecked(), "marker click changed attendance state") ||
      !Check(markerButton->height() <= 22 &&
                 markerButton->text() == QString::fromUtf8("Т+"),
             "combined trainer marker is not scan-visible") ||
      !Check(markerButton->toolTip().contains("Оплата") &&
                 markerButton->toolTip().contains("Первое посещение") &&
                 markerButton->toolTip().contains("Вёл тренировку") &&
                 markerButton->toolTip().contains("&lt;оплачено&gt;"),
             "marker tooltip lost semantics or escaping"))
  {
    return false;
  }
  cell.setEditable(false);
  return Check(!checkBox->isEnabled() && !markerButton->isEnabled(),
               "read-only marker cell remains editable");
}

bool TrainerMarkerUsesDedicatedBadge()
{
  const ParticipantId id{"12345678-1234-1234-1234-123456789abc"};
  const ParticipantDayMarker marker{
      id, 16, DayMarkerKind::LedTraining, QString()};
  AttendanceCellWidget cell(false, "Alice", QDate(2026, 7, 16), marker);
  const auto* markerButton = cell.markerButton();
  return Check(markerButton && markerButton->text() ==
                                  QString::fromUtf8("Т") &&
                   markerButton->toolTip().contains("Вёл тренировку"),
               "trainer marker has no readable dedicated badge");
}

bool ParticipantEditorUsesReasonableYearAndKeepsIdInDetails()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.fullName = "Alice Example";
  profile.contact = "@alice";
  profile.rank = ParticipantRank::Guest;
  ParticipantDialog dialog(profile, true);
  auto* year = dialog.findChild<QSpinBox*>("participantBirthYearSpinBox");
  auto* rank = dialog.findChild<QComboBox*>("participantRankComboBox");
  auto* id = dialog.findChild<QLabel*>("participantIdLabel");
  auto* fullName = dialog.findChild<QLineEdit*>("participantFullNameEdit");
  auto* contact = dialog.findChild<QLineEdit*>("participantContactEdit");
  if (!Check(year && rank && id && fullName && contact,
             "participant profile controls missing"))
  {
    return false;
  }
  if (!Check(year->minimum() == 1899 &&
                 year->specialValueText() == QString::fromUtf8("Не указан") &&
                 year->maximum() == QDate::currentDate().year(),
             "participant birth year range is unreasonable") ||
      !Check(id->text() == profile.id.value,
             "participant details do not expose full ID") ||
      !Check(fullName->text() == profile.fullName &&
                 fullName->maxLength() == kMaxParticipantFullNameLength &&
                 contact->text() == profile.contact &&
                 contact->maxLength() == kMaxParticipantContactLength,
             "participant details did not populate new profile fields"))
  {
    return false;
  }
  const int knightIndex =
      rank->findData(static_cast<int>(ParticipantRank::Knight));
  if (!Check(knightIndex >= 0, "Knight rank missing from participant editor"))
  {
    return false;
  }
  rank->setCurrentIndex(knightIndex);
  fullName->setText("Alice Updated");
  contact->setText("+7 900 000-00-00");
  const ParticipantProfile edited = dialog.profile();
  return Check(edited.rank == ParticipantRank::Knight &&
                   edited.fullName == "Alice Updated" &&
                   edited.contact == "+7 900 000-00-00",
               "participant editor lost profile details");
}

bool ParticipantDirectoryHidesIdAndSortsByRank()
{
  ParticipantProfile knight;
  knight.id = {"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"};
  knight.displayName = "Knight";
  knight.fullName = "Knight Full Name";
  knight.rank = ParticipantRank::Knight;
  ParticipantProfile page;
  page.id = {"bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"};
  page.displayName = "Page";
  page.rank = ParticipantRank::Page;
  ParticipantDirectoryDialog dialog({knight, page});
  auto* table = dialog.findChild<QTableWidget*>();
  if (!Check(table && table->columnCount() == 4,
             "participant directory column count is invalid"))
  {
    return false;
  }
  return Check(table->horizontalHeaderItem(0)->text() ==
                       QString::fromUtf8("Историчное имя") &&
                   table->horizontalHeaderItem(1)->text() ==
                       QString::fromUtf8("ФИО") &&
                   table->horizontalHeaderItem(2)->text() ==
                       QString::fromUtf8("Звание") &&
                   table->horizontalHeaderItem(3)->text() ==
                       QString::fromUtf8("Статус"),
               "participant directory exposes ID or misses rank") &&
         Check(table->item(0, 0)->text() == "Page" &&
                   table->item(1, 0)->text() == "Knight",
               "participant directory is not sorted by rank") &&
         Check(table->item(1, 1)->text() == "Knight Full Name",
               "participant directory lost full name") &&
         Check(table->item(0, 0)->data(Qt::UserRole).toString() ==
                   page.id.value,
               "participant directory lost hidden row identity");
}

bool EventEditorSupportsInternalAndFreeBoutSides()
{
  ParticipantProfile petya;
  petya.id = {"11111111-1111-1111-1111-111111111111"};
  petya.displayName = "Петя";
  petya.fullName = "Пётр Петров";
  ParticipantProfile namesake;
  namesake.id = {"22222222-2222-2222-2222-222222222222"};
  namesake.displayName = "Петя";
  namesake.fullName = "Пётр Петров";
  EventRecord event;
  event.id = CreateEventId();
  event.date = QDate(2026, 7, 18);
  EventDialog dialog(event, {petya, namesake});
  auto* title = dialog.findChild<QLineEdit*>("eventTitleEdit");
  auto* addBout = dialog.findChild<QPushButton*>("addEventBoutButton");
  if (!Check(title && addBout, "event editor controls missing"))
  {
    return false;
  }
  dialog.show();
  QApplication::processEvents();
  title->setText("Турнир");
  addBout->click();
  QApplication::processEvents();
  QApplication::processEvents();
  const auto sides = dialog.findChildren<QComboBox*>("eventBoutSideCombo");
  const auto scoreA = dialog.findChildren<QSpinBox*>("eventBoutScoreA");
  const auto scoreB = dialog.findChildren<QSpinBox*>("eventBoutScoreB");
  const auto removeButtons =
      dialog.findChildren<QPushButton*>("removeEventBoutButton");
  if (!Check(sides.size() == 2 && scoreA.size() == 1 &&
                 scoreB.size() == 1 && removeButtons.size() == 1,
             "event bout controls missing"))
  {
    return false;
  }
  const auto* boutsTable =
      dialog.findChild<QTableWidget*>("eventBoutsTable");
  if (!Check(boutsTable && boutsTable->rowHeight(0) >= 44 &&
                 sides.at(0)->width() >= 160 &&
                 sides.at(1)->width() >= 160 &&
                 sides.at(0)->height() >= 30 &&
                 sides.at(1)->height() >= 30 &&
                 scoreA.at(0)->width() >= scoreA.at(0)->sizeHint().width() &&
                 scoreB.at(0)->width() >= scoreB.at(0)->sizeHint().width() &&
                 removeButtons.at(0)->width() >=
                     removeButtons.at(0)->sizeHint().width(),
             "new bout controls have broken initial geometry"))
  {
    return false;
  }
  const int petyaIndex = sides.at(0)->findData(petya.id.value);
  const int namesakeIndex = sides.at(0)->findData(namesake.id.value);
  if (!Check(petyaIndex >= 0 && namesakeIndex >= 0 &&
                 sides.at(0)->itemText(petyaIndex) !=
                     sides.at(0)->itemText(namesakeIndex),
             "duplicate participant labels are ambiguous in bout editor"))
  {
    return false;
  }
  sides.at(0)->setCurrentIndex(petyaIndex);
  auto* roster = dialog.findChild<QListWidget*>("eventParticipantsList");
  QListWidgetItem* linkedParticipant = nullptr;
  if (roster)
  {
    for (int row = 0; row < roster->count(); ++row)
    {
      if (roster->item(row)->data(Qt::UserRole).toString() == petya.id.value)
      {
        linkedParticipant = roster->item(row);
        break;
      }
    }
  }
  if (!Check(linkedParticipant &&
                 linkedParticipant->checkState() == Qt::Checked &&
                 !(linkedParticipant->flags() & Qt::ItemIsUserCheckable),
             "bout-linked participant is not visibly required in roster"))
  {
    return false;
  }
  sides.at(1)->setCurrentIndex(-1);
  const QString internalLabel =
      sides.at(1)->itemText(sides.at(1)->findData(petya.id.value));
  sides.at(1)->setEditText(internalLabel);
  const EventRecord externalNamesake = dialog.eventRecord();
  if (!Check(!externalNamesake.bouts.front().sideB.participantId.has_value() &&
                 externalNamesake.bouts.front().sideB.freeName ==
                     internalLabel,
             "free-text namesake was silently converted to internal UUID"))
  {
    return false;
  }
  sides.at(1)->setEditText("Вася из другого клуба");
  scoreA.front()->setValue(5);
  scoreB.front()->setValue(20);
  const EventRecord edited = dialog.eventRecord();
  if (!Check(edited.isValid() && edited.participants.size() == 1 &&
                 edited.bouts.size() == 1 &&
                 edited.bouts.front().sideA.participantId == petya.id &&
                 edited.bouts.front().sideB.freeName ==
                     "Вася из другого клуба" &&
                 edited.bouts.front().scoreA == 5 &&
                 edited.bouts.front().scoreB == 20,
             "event editor lost linked/free sides or score"))
  {
    return false;
  }

  petya.displayName = "Петя после переименования";
  petya.fullName = "Пётр После-Переименования";
  EventDialog reopened(edited, {petya});
  const EventRecord preserved = reopened.eventRecord();
  return Check(preserved.participants.size() == 1 &&
                   preserved.participants.front().displayNameSnapshot ==
                       "Петя" &&
                   preserved.participants.front().fullNameSnapshot ==
                       "Пётр Петров",
               "editing event silently rewrote historical name snapshot");
}

} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  if (!WeekdayClickSelectsAndClearsGroup() || !BulkControlsStaySynchronized() ||
      !DayMarkerDialogSupportsMultipleKindsAndClear() ||
      !NoteOnlyMarkerBecomesOther() ||
      !AttendanceCellUsesCompactSemanticBadge() ||
      !TrainerMarkerUsesDedicatedBadge() ||
      !ParticipantEditorUsesReasonableYearAndKeepsIdInDetails() ||
      !ParticipantDirectoryHidesIdAndSortsByRank() ||
      !EventEditorSupportsInternalAndFreeBoutSides())
  {
    return 1;
  }
  return 0;
}
