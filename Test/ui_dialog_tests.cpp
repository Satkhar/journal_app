#include "AttendanceCellWidget.hpp"
#include "CopyUsersDialog.hpp"
#include "DayMarkerDialog.hpp"
#include "EventDialog.hpp"
#include "MonthDaysDialog.hpp"
#include "ParticipantDialog.hpp"
#include "ParticipantDirectoryDialog.hpp"
#include "ParticipantEmblemWidget.hpp"
#include "ParticipantStrikeHistoryDialog.hpp"
#include "ParticipantStatisticsWidget.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDate>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QFile>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTextEdit>

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

ParticipantEmblem MakeParticipantEmblem(const ParticipantId& participantId,
                                        qint64 revision)
{
  QImage image(2, 2, QImage::Format_ARGB32);
  image.fill(QColor(30, 80, 160));
  QByteArray imageData;
  QBuffer buffer(&imageData);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");

  ParticipantEmblem emblem;
  emblem.participantId = participantId;
  emblem.imageData = imageData;
  emblem.sha256 =
      QCryptographicHash::hash(imageData, QCryptographicHash::Sha256);
  emblem.originalFileName = "herb.png";
  emblem.pixelWidth = image.width();
  emblem.pixelHeight = image.height();
  emblem.revision = revision;
  return emblem;
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

bool AddParticipantsDialogUsesConfiguredMonthDropdown()
{
  CopyUsersDialog dialog(2026, 7, {{2025, 12}, {2026, 7}, {2026, 6}, {2026, 6}},
                         nullptr, true);
  auto* source = dialog.findChild<QComboBox*>("copyUsersSourceMonthCombo");
  auto* copyWeekdays =
      dialog.findChild<QCheckBox*>("copyWeekdayPatternCheckBox");
  if (!Check(source && copyWeekdays,
             "add-participants dialog controls missing"))
  {
    return false;
  }
  if (!Check(source->count() == 2,
             "source dropdown contains target or duplicate month") ||
      !Check(source->findData(QDate(2026, 7, 1)) < 0,
             "target month remains selectable") ||
      !Check(dialog.sourceYear() == 2026 && dialog.sourceMonth() == 6,
             "previous formed month is not selected by default") ||
      !Check(copyWeekdays->isChecked(), "weekday-copy default was not applied"))
  {
    return false;
  }

  source->setCurrentIndex(source->findData(QDate(2025, 12, 1)));
  return Check(dialog.sourceYear() == 2025 && dialog.sourceMonth() == 12,
               "dropdown did not expose selected source month");
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
  if (!Check(special->text() ==
                 QString::fromUtf8("Тренировка в доспехах"),
             "armor-training marker uses stale terminology"))
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
  const ParticipantDayMarker marker{id, 16,
                                    DayMarkerKind::Payment |
                                        DayMarkerKind::SpecialTraining |
                                        DayMarkerKind::FirstVisit |
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
                 markerButton->toolTip().contains(
                     "Тренировка в доспехах") &&
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
  const ParticipantDayMarker marker{id, 16, DayMarkerKind::LedTraining,
                                    QString()};
  AttendanceCellWidget cell(false, "Alice", QDate(2026, 7, 16), marker);
  const auto* markerButton = cell.markerButton();
  return Check(markerButton && markerButton->text() == QString::fromUtf8("Т") &&
                   markerButton->toolTip().contains("Вёл тренировку"),
               "trainer marker has no readable dedicated badge");
}

bool AttendanceCellHighlightsVisitAndSupportsContextMarkerEdit()
{
  AttendanceCellWidget cell(false, "Alice", QDate(2026, 7, 16),
                            std::nullopt);
  auto* checkBox = cell.attendanceCheckBox();
  auto* markerButton = cell.markerButton();
  if (!Check(checkBox && markerButton, "attendance cell controls missing") ||
      !Check(markerButton->styleSheet().contains("#ECEFF1"),
             "empty marker button is not light gray"))
  {
    return false;
  }

  checkBox->setChecked(true);
  if (!Check(cell.styleSheet().contains("#DFF2DF"),
             "checked attendance cell is not highlighted"))
  {
    return false;
  }
  cell.setAttendanceChecked(false);
  if (!Check(!checkBox->isChecked() &&
                 cell.styleSheet().contains("background: transparent"),
             "programmatic attendance rollback left stale highlight"))
  {
    return false;
  }

  bool markerEditRequested = false;
  QObject::connect(&cell, &AttendanceCellWidget::markerEditRequested, &cell,
                   [&markerEditRequested]() { markerEditRequested = true; });
  QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(1, 1),
                          QPoint(1, 1), Qt::NoModifier);
  QApplication::sendEvent(&cell, &event);
  return Check(markerEditRequested,
               "cell context menu did not request marker editing");
}

ParticipantJournalStatistics MakeJournalStatistics(
    const ParticipantId& participantId)
{
  ParticipantMonthStatistics july;
  july.month = {2026, 7};
  july.trackedDayCount = 16;
  july.attendedDayCount = 9;
  july.specialTrainingVisitCount = 2;
  july.ledTrainingDayCount = 1;

  ParticipantMonthStatistics december;
  december.month = {2025, 12};
  december.trackedDayCount = 12;
  december.attendedDayCount = 0;

  ParticipantJournalStatistics result;
  result.participantId = participantId;
  result.months = {july, december};
  result.totalAttendedDayCount = 9;
  result.totalSpecialTrainingVisitCount = 2;
  result.totalLedTrainingDayCount = 1;
  result.firstRecordedVisit = QDate(2026, 7, 2);
  result.lastRecordedVisit = QDate(2026, 7, 28);
  return result;
}

ParticipantEventStatistics MakeEventStatistics(
    const ParticipantId& participantId)
{
  ParticipantEventStatistics result;
  result.participantId = participantId;
  result.tournamentCount = 4;
  result.clubTournamentCount = 1;
  result.externalCompetitionCount = 1;
  result.softCombatTournamentCount = 1;
  result.unspecifiedTournamentCount = 1;
  result.nonCompetingTripCount = 2;
  result.boutCount = 8;
  result.firstTournament = QDate(2025, 9, 20);
  result.lastTournament = QDate(2026, 6, 14);
  return result;
}

bool ParticipantStatisticsShowsAccessibleMonthHistory()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  const QDate currentMonth(QDate::currentDate().year(),
                           QDate::currentDate().month(), 1);
  const QDate trainingStart = currentMonth.addMonths(-14);
  profile.trainingStartMonth =
      JournalMonth{trainingStart.year(), trainingStart.month()};
  const ParticipantJournalStatistics journal =
      MakeJournalStatistics(profile.id);
  const ParticipantEventStatistics events = MakeEventStatistics(profile.id);
  const ParticipantStatisticsData statistics{journal, {}, events, {}};
  ParticipantDialog dialog(profile, statistics, true);

  auto* tabs = dialog.findChild<QTabWidget*>("participantTabWidget");
  auto* total =
      dialog.findChild<QLabel*>("participantTotalAttendanceLabel");
  auto* average =
      dialog.findChild<QLabel*>("participantAverageAttendanceLabel");
  auto* tournaments =
      dialog.findChild<QLabel*>("participantTournamentCountLabel");
  auto* clubTournaments =
      dialog.findChild<QLabel*>("participantClubTournamentCountLabel");
  auto* externalCompetitions = dialog.findChild<QLabel*>(
      "participantExternalCompetitionCountLabel");
  auto* softCombatTournaments = dialog.findChild<QLabel*>(
      "participantSoftCombatTournamentCountLabel");
  auto* unspecifiedTournaments = dialog.findChild<QLabel*>(
      "participantUnspecifiedTournamentCountLabel");
  auto* nonCompetingTrips = dialog.findChild<QLabel*>(
      "participantNonCompetingTripCountLabel");
  auto* trainingStartLabel =
      dialog.findChild<QLabel*>("participantTrainingStartLabel");
  auto* trainingDuration =
      dialog.findChild<QLabel*>("participantTrainingDurationLabel");
  auto* july =
      dialog.findChild<QToolButton*>("participantMonthButton_2026_07");
  auto* december =
      dialog.findChild<QToolButton*>("participantMonthButton_2025_12");
  auto* content =
      dialog.findChild<QWidget*>("participantMonthStatisticsContent");
  auto* grid = content ? qobject_cast<QGridLayout*>(content->layout())
                       : nullptr;
  if (!Check(tabs && total && average && tournaments && clubTournaments &&
                 externalCompetitions && softCombatTournaments &&
                 unspecifiedTournaments && nonCompetingTrips &&
                 trainingStartLabel && trainingDuration && july && december &&
                 grid,
             "participant statistics controls missing"))
  {
    return false;
  }

  int julyRow = -1;
  int julyColumn = -1;
  int julyRowSpan = 0;
  int julyColumnSpan = 0;
  int decemberRow = -1;
  int decemberColumn = -1;
  int decemberRowSpan = 0;
  int decemberColumnSpan = 0;
  grid->getItemPosition(grid->indexOf(july), &julyRow, &julyColumn,
                        &julyRowSpan, &julyColumnSpan);
  grid->getItemPosition(grid->indexOf(december), &decemberRow,
                        &decemberColumn, &decemberRowSpan,
                        &decemberColumnSpan);

  return Check(tabs->count() == 2 && tabs->tabText(1) ==
                                           QString::fromUtf8("Статистика"),
               "participant statistics tab missing") &&
         Check(total->text() == "9" && !average->text().isEmpty() &&
                    tournaments->text() == "4" &&
                    clubTournaments->text() == "1" &&
                    externalCompetitions->text() == "1" &&
                    softCombatTournaments->text() == "1" &&
                    unspecifiedTournaments->text() == "1" &&
                    nonCompetingTrips->text() == "2",
               "participant statistics totals are wrong") &&
          Check(trainingStartLabel->text().contains(
                    QString::number(trainingStart.year())) &&
                    trainingDuration->text() ==
                        QString::fromUtf8("≈ 1 г. 2 мес."),
                "manual training start is not reflected in experience") &&
         Check(july->text().contains("9/16") &&
                   july->toolTip().contains("Посещено: 9") &&
                   july->toolTip().contains(
                       "Дат учёта в месяце состава: 16") &&
                   july->toolTip().contains("Тренировки в доспехах: 2") &&
                   july->accessibleName().contains("Открыть месяц") &&
                   july->focusPolicy() == Qt::StrongFocus,
               "active month cell is not readable or accessible") &&
         Check(december->text().contains("0/12") &&
                   december->styleSheet().contains("#ECEFF1"),
               "zero-attendance month is missing or visually ambiguous") &&
         Check(decemberRow < julyRow && decemberColumn == 12 &&
                   julyColumn == 7,
               "month history is not grouped chronologically by year");
}

bool ParticipantAverageUsesOnlyCompletedMonths()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";

  const QDate today = QDate::currentDate();
  const QDate currentMonth(today.year(), today.month(), 1);
  const QDate previousMonth = currentMonth.addMonths(-1);
  const QDate futureMonth = currentMonth.addMonths(1);

  ParticipantMonthStatistics completed;
  completed.month = {previousMonth.year(), previousMonth.month()};
  completed.trackedDayCount = 8;
  completed.attendedDayCount = 4;

  ParticipantMonthStatistics current;
  current.month = {currentMonth.year(), currentMonth.month()};
  current.trackedDayCount = 12;
  current.attendedDayCount = 10;

  ParticipantMonthStatistics future;
  future.month = {futureMonth.year(), futureMonth.month()};
  future.trackedDayCount = 12;
  future.attendedDayCount = 12;

  ParticipantJournalStatistics journal;
  journal.participantId = profile.id;
  journal.months = {completed, current, future};
  journal.totalAttendedDayCount = 26;

  const ParticipantStatisticsData statistics{
      journal, {}, std::nullopt, "Турниры не загружались"};
  ParticipantDialog dialog(profile, statistics, false);
  const auto* average =
      dialog.findChild<QLabel*>("participantAverageAttendanceLabel");
  return Check(average && average->text() == QString::fromUtf8("4,0"),
               "current or future month affected completed-month average");
}

bool ParticipantMonthNavigationReturnsSelectionAndProtectsDirtyProfile()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  profile.fullName = "Alice Example";
  const ParticipantJournalStatistics journal =
      MakeJournalStatistics(profile.id);
  const ParticipantStatisticsData statistics{
      journal, {}, std::nullopt, "Турниры не загружались"};

  ParticipantDialog cleanDialog(profile, statistics, true);
  auto* cleanMonth = cleanDialog.findChild<QToolButton*>(
      "participantMonthButton_2026_07");
  if (!Check(cleanMonth != nullptr, "month navigation control missing"))
  {
    return false;
  }
  cleanMonth->click();
  const auto selected = cleanDialog.selectedMonth();
  if (!Check(cleanDialog.result() == QDialog::Accepted &&
                 cleanDialog.action() == ParticipantDialog::Action::OpenMonth &&
                 selected.has_value() && selected->year == 2026 &&
                 selected->month == 7,
             "month click did not return a navigation request"))
  {
    return false;
  }

  ParticipantDialog dirtyDialog(profile, statistics, true);
  auto* fullName =
      dirtyDialog.findChild<QLineEdit*>("participantFullNameEdit");
  auto* dirtyMonth = dirtyDialog.findChild<QToolButton*>(
      "participantMonthButton_2026_07");
  if (!Check(fullName && dirtyMonth,
             "dirty-profile navigation controls missing"))
  {
    return false;
  }
  fullName->setText("Alice Changed");
  bool warningShown = false;
  QTimer::singleShot(
      0, &dirtyDialog,
      [&warningShown]()
      {
        auto* warning =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (warning)
        {
          warningShown = true;
          warning->accept();
        }
      });
  dirtyMonth->click();
  return Check(warningShown &&
                   dirtyDialog.action() == ParticipantDialog::Action::Cancel &&
                   !dirtyDialog.selectedMonth().has_value(),
               "dirty profile was silently discarded by month navigation");
}

bool ParticipantStatisticsDistinguishesUnavailableSources()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  ParticipantStatisticsData statistics;
  statistics.journalError = "Ошибка чтения журнала";
  statistics.eventError = "Ошибка чтения турниров";
  ParticipantDialog dialog(profile, statistics, false);
  const auto* journalError = dialog.findChild<QLabel*>(
      "participantJournalStatisticsUnavailableLabel");
  const auto* eventError = dialog.findChild<QLabel*>(
      "participantEventStatisticsUnavailableLabel");
  return Check(journalError &&
                   journalError->text().contains(statistics.journalError),
               "journal statistics error reason is hidden") &&
         Check(eventError &&
                   eventError->text().contains(statistics.eventError),
               "event statistics error reason is hidden") &&
         Check(dialog.findChild<QLabel*>(
                   "participantMonthStatisticsEmptyLabel") != nullptr,
               "missing month statistics have no explicit state");
}

bool ParticipantEditorUsesReasonableYearAndKeepsIdInDetails()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice Example";
  profile.fullName = "Alice Example";
  profile.contact = "@alice";
  profile.rank = ParticipantRank::Guest;
  profile.trainingStartMonth = JournalMonth{2018, 9};
  ParticipantDialog dialog(profile, true);
  auto* year = dialog.findChild<QSpinBox*>("participantBirthYearSpinBox");
  auto* rank = dialog.findChild<QComboBox*>("participantRankComboBox");
  auto* combatHand =
      dialog.findChild<QComboBox*>("participantCombatHandComboBox");
  auto* id = dialog.findChild<QLabel*>("participantIdLabel");
  auto* historicalName =
      dialog.findChild<QLineEdit*>("participantHistoricalNameEdit");
  auto* fullName = dialog.findChild<QLineEdit*>("participantFullNameEdit");
  auto* contact = dialog.findChild<QLineEdit*>("participantContactEdit");
  auto* trainingStartCheck =
      dialog.findChild<QCheckBox*>("participantTrainingStartCheckBox");
  auto* trainingStartMonth = dialog.findChild<QComboBox*>(
      "participantTrainingStartMonthComboBox");
  auto* trainingStartYear = dialog.findChild<QSpinBox*>(
      "participantTrainingStartYearSpinBox");
  auto* trainingStartLabel =
      dialog.findChild<QLabel*>("participantTrainingStartLabel");
  if (!Check(year && rank && combatHand && id && historicalName && fullName &&
                 contact && trainingStartCheck && trainingStartMonth &&
                 trainingStartYear && trainingStartLabel,
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
      !Check(historicalName->text().isEmpty() &&
                 fullName->text() == profile.fullName &&
                 fullName->maxLength() == kMaxParticipantFullNameLength &&
                 contact->text() == profile.contact &&
                 contact->maxLength() == kMaxParticipantContactLength,
             "participant details did not populate new profile fields") ||
      !Check(trainingStartCheck->isChecked() &&
                 trainingStartMonth->currentData().toInt() == 9 &&
                 trainingStartYear->value() == 2018 &&
                 trainingStartYear->minimum() == 1900 &&
                 trainingStartYear->maximum() == QDate::currentDate().year(),
             "training-start month was clamped or not populated"))
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
  combatHand->setCurrentIndex(
      combatHand->findData(static_cast<int>(CombatHand::Left)));
  fullName->setText("Alice Updated");
  historicalName->setText("Alicia");
  contact->setText("+7 900 000-00-00");
  trainingStartMonth->setCurrentIndex(trainingStartMonth->findData(4));
  trainingStartYear->setValue(2017);
  const ParticipantProfile edited = dialog.profile();
  if (!Check(edited.rank == ParticipantRank::Knight &&
                   edited.combatHand == CombatHand::Left &&
                   edited.historicalName == "Alicia" &&
                   edited.displayName == "Alicia" &&
                   edited.fullName == "Alice Updated" &&
                   edited.contact == "+7 900 000-00-00" &&
                   edited.trainingStartMonth == JournalMonth{2017, 4} &&
                   trainingStartLabel->text().contains("2017"),
             "participant editor lost profile details"))
  {
    return false;
  }
  trainingStartCheck->setChecked(false);
  return Check(!dialog.profile().trainingStartMonth.has_value() &&
                   !trainingStartMonth->isEnabled() &&
                   !trainingStartYear->isEnabled() &&
                   trainingStartLabel->text() ==
                       QString::fromUtf8("Не указано"),
               "participant editor cannot explicitly clear training start");
}

bool ParticipantEditorRejectsFutureTrainingStart()
{
  const QDate today = QDate::currentDate();
  if (today.month() == 12)
  {
    return true;
  }

  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  ParticipantDialog dialog(profile, true);
  auto* trainingStartCheck =
      dialog.findChild<QCheckBox*>("participantTrainingStartCheckBox");
  auto* trainingStartMonth = dialog.findChild<QComboBox*>(
      "participantTrainingStartMonthComboBox");
  auto* trainingStartYear = dialog.findChild<QSpinBox*>(
      "participantTrainingStartYearSpinBox");
  QPushButton* saveButton = nullptr;
  for (QPushButton* button : dialog.findChildren<QPushButton*>())
  {
    if (button->text() == QString::fromUtf8("Сохранить"))
    {
      saveButton = button;
      break;
    }
  }
  if (!Check(trainingStartCheck && trainingStartMonth && trainingStartYear &&
                 saveButton,
             "future training-start test controls missing"))
  {
    return false;
  }

  trainingStartCheck->setChecked(true);
  trainingStartYear->setValue(today.year());
  trainingStartMonth->setCurrentIndex(
      trainingStartMonth->findData(today.month() + 1));
  bool warningShown = false;
  QTimer::singleShot(
      0, &dialog,
      [&warningShown]()
      {
        auto* warning =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (warning)
        {
          warningShown = warning->windowTitle() ==
                         QString::fromUtf8(
                             "Некорректное начало тренировок");
          warning->accept();
        }
      });
  saveButton->click();
  return Check(warningShown &&
                   dialog.action() == ParticipantDialog::Action::Cancel,
               "future training-start month was accepted by editor");
}

bool ParticipantEditorPreservesStoredFutureTrainingStart()
{
  const QDate futureMonth =
      QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1)
          .addYears(1);
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  profile.trainingStartMonth =
      JournalMonth{futureMonth.year(), futureMonth.month()};

  ParticipantDialog dialog(profile, true);
  const auto* trainingStartMonth = dialog.findChild<QComboBox*>(
      "participantTrainingStartMonthComboBox");
  const auto* trainingStartYear = dialog.findChild<QSpinBox*>(
      "participantTrainingStartYearSpinBox");
  return Check(trainingStartMonth && trainingStartYear &&
                   trainingStartYear->maximum() == futureMonth.year() &&
                   trainingStartYear->value() == futureMonth.year() &&
                   trainingStartMonth->currentData().toInt() ==
                       futureMonth.month() &&
                   dialog.profile().trainingStartMonth ==
                       profile.trainingStartMonth,
               "stored future training start was silently clamped by UI");
}

bool ParticipantEditorWarnsAboutStartAfterFirstRecordedVisit()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";

  const QDate currentMonth(QDate::currentDate().year(),
                           QDate::currentDate().month(), 1);
  const QDate firstVisitMonth = currentMonth.addMonths(-2);
  const QDate declaredStart = firstVisitMonth.addMonths(1);
  ParticipantJournalStatistics journal;
  journal.participantId = profile.id;
  journal.firstRecordedVisit =
      QDate(firstVisitMonth.year(), firstVisitMonth.month(), 15);
  const ParticipantStatisticsData statistics{
      journal, {}, std::nullopt, "Турниры не загружались"};
  ParticipantDialog dialog(profile, statistics, true);
  auto* trainingStartCheck =
      dialog.findChild<QCheckBox*>("participantTrainingStartCheckBox");
  auto* trainingStartMonth = dialog.findChild<QComboBox*>(
      "participantTrainingStartMonthComboBox");
  auto* trainingStartYear = dialog.findChild<QSpinBox*>(
      "participantTrainingStartYearSpinBox");
  const auto* consistencyWarning = dialog.findChild<QLabel*>(
      "participantTrainingStartConsistencyWarning");
  QPushButton* saveButton = nullptr;
  for (QPushButton* button : dialog.findChildren<QPushButton*>())
  {
    if (button->text() == QString::fromUtf8("Сохранить"))
    {
      saveButton = button;
      break;
    }
  }
  if (!Check(trainingStartCheck && trainingStartMonth && trainingStartYear &&
                 consistencyWarning && saveButton,
             "first-visit consistency test controls missing"))
  {
    return false;
  }

  trainingStartCheck->setChecked(true);
  trainingStartYear->setValue(declaredStart.year());
  trainingStartMonth->setCurrentIndex(
      trainingStartMonth->findData(declaredStart.month()));
  if (!Check(!consistencyWarning->isHidden() &&
                 consistencyWarning->text().contains(
                     QString::fromUtf8("первое посещение")),
             "statistics hide contradictory manual training start"))
  {
    return false;
  }
  bool warningShown = false;
  QTimer::singleShot(
      0, &dialog,
      [&warningShown]()
      {
        auto* warning =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (warning)
        {
          auto* saveAnywayButton = warning->button(QMessageBox::Save);
          warningShown =
              warning->windowTitle() ==
                  QString::fromUtf8("Проверьте начало тренировок") &&
              warning->text().contains(
                  QString::fromUtf8("первого посещения")) &&
              saveAnywayButton != nullptr;
          if (saveAnywayButton)
          {
            saveAnywayButton->click();
          }
        }
      });
  saveButton->click();
  return Check(warningShown &&
                   dialog.action() == ParticipantDialog::Action::Save,
               "training-start contradiction was not explicitly warned");
}

bool EmblemNormalizerScalesAndCanonicalizesImage()
{
  QTemporaryDir directory;
  const QString path = directory.filePath("large-heraldry.png");
  QImage source(2048, 1536, QImage::Format_ARGB32);
  source.fill(QColor(20, 70, 140));
  if (!Check(directory.isValid() && source.save(path, "PNG"),
             "large emblem fixture could not be written"))
  {
    return false;
  }

  const ParticipantId participantId{
      "12345678-1234-1234-1234-123456789abc"};
  QString error;
  const auto normalized =
      NormalizeParticipantEmblemImage(participantId, path, 9, &error);
  QImage decoded;
  if (normalized.has_value())
  {
    decoded.loadFromData(normalized->imageData, "PNG");
  }
  return Check(normalized.has_value() && normalized->isValid() &&
                   normalized->participantId == participantId &&
                   normalized->revision == 9 &&
                   normalized->originalFileName == "large-heraldry.png" &&
                   normalized->pixelWidth == 1024 &&
                   normalized->pixelHeight == 768 && !decoded.isNull() &&
                   decoded.size() == QSize(1024, 768),
               "emblem normalization lost identity, revision, or bounds");
}

bool EmblemNormalizerRejectsCorruptData()
{
  QTemporaryDir directory;
  const QString path = directory.filePath("corrupt.png");
  QFile file(path);
  if (!Check(directory.isValid() && file.open(QIODevice::WriteOnly) &&
                 file.write("this is not an image") > 0,
             "corrupt emblem fixture could not be written"))
  {
    return false;
  }
  file.close();

  QString error;
  const auto normalized = NormalizeParticipantEmblemImage(
      {"12345678-1234-1234-1234-123456789abc"}, path, 0, &error);
  return Check(!normalized.has_value() && !error.trimmed().isEmpty(),
               "corrupt emblem data was accepted");
}

bool EmblemNormalizerRejectsOversizedSource()
{
  QTemporaryDir directory;
  const QString path = directory.filePath("oversized.png");
  QImage source(4097, 2, QImage::Format_ARGB32);
  source.fill(Qt::red);
  if (!Check(directory.isValid() && source.save(path, "PNG"),
             "oversized emblem fixture could not be written"))
  {
    return false;
  }

  QString error;
  const auto normalized = NormalizeParticipantEmblemImage(
      {"12345678-1234-1234-1234-123456789abc"}, path, 0, &error);
  return Check(!normalized.has_value() &&
                   error.contains(QString::fromUtf8("размер"),
                                  Qt::CaseInsensitive),
               "emblem source above 4096 px was accepted");
}

bool ReadOnlyMissingEmblemIsExplicitlyUnavailable()
{
  ParticipantEmblemWidget widget(
      {"12345678-1234-1234-1234-123456789abc"}, std::nullopt, false);
  const auto* preview =
      widget.findChild<QLabel*>("participantEmblemPreview");
  const auto* details =
      widget.findChild<QLabel*>("participantEmblemDetails");
  const auto* choose =
      widget.findChild<QPushButton*>("participantEmblemChooseButton");
  const auto* remove =
      widget.findChild<QPushButton*>("participantEmblemRemoveButton");
  return Check(preview &&
                   preview->text().contains(QString::fromUtf8("недоступен")) &&
                   details &&
                   details->text().contains(QString::fromUtf8("локальном")) &&
                   choose && !choose->isEnabled() && remove &&
                   !remove->isEnabled(),
               "read-only missing emblem looks like an editable empty value");
}

bool ExistingParticipantEmblemCanBeDisplayedAndRemoved()
{
  const ParticipantId participantId{
      "12345678-1234-1234-1234-123456789abc"};
  const ParticipantEmblem emblem = MakeParticipantEmblem(participantId, 7);
  if (!Check(emblem.isValid(), "emblem fixture is invalid"))
  {
    return false;
  }

  ParticipantEmblemWidget widget(participantId, emblem, true);
  auto* details = widget.findChild<QLabel*>("participantEmblemDetails");
  auto* choose =
      widget.findChild<QPushButton*>("participantEmblemChooseButton");
  auto* remove =
      widget.findChild<QPushButton*>("participantEmblemRemoveButton");
  bool changed = false;
  QObject::connect(&widget, &ParticipantEmblemWidget::changed, &widget,
                   [&changed]() { changed = true; });
  if (!Check(details && details->text().contains(emblem.originalFileName) &&
                 choose && choose->isEnabled() && remove && remove->isEnabled(),
             "existing emblem is not exposed by the editor"))
  {
    return false;
  }

  remove->click();
  return Check(changed &&
                   widget.action() == ParticipantEmblemAction::Remove &&
                   !widget.emblem().has_value() &&
                   widget.expectedRevision() == emblem.revision,
               "removing an emblem lost the staged CAS mutation");
}

bool TimedStrikeEditorBuildsValidUtcRecord()
{
  TimedStrikeTest source;
  source.id = CreateTimedStrikeTestId();
  source.participantId = {
      "12345678-1234-1234-1234-123456789abc"};
  source.performedAt =
      QDateTime::fromString("2026-07-21T10:15:00Z", Qt::ISODate);
  source.hand = StrikeHand::Right;
  source.strikeCount = 100;
  source.durationSeconds = 30;
  source.weapon = StrikeWeapon::Sword;
  source.revision = 3;
  if (!Check(source.isValid(), "strike-test fixture is invalid"))
  {
    return false;
  }

  TimedStrikeTestDialog dialog(source);
  auto* performedAt =
      dialog.findChild<QDateTimeEdit*>("strikeTestPerformedAtEdit");
  auto* hand = dialog.findChild<QComboBox*>("strikeTestHandCombo");
  auto* count = dialog.findChild<QSpinBox*>("strikeTestCountSpin");
  auto* duration = dialog.findChild<QSpinBox*>("strikeTestDurationSpin");
  auto* weapon = dialog.findChild<QComboBox*>("strikeTestWeaponCombo");
  auto* note = dialog.findChild<QTextEdit*>("strikeTestNoteEdit");
  if (!Check(performedAt && hand && count && duration && weapon && note,
             "strike-test editor controls are missing"))
  {
    return false;
  }

  const QDateTime expectedUtc =
      QDateTime::fromString("2026-08-03T07:45:00Z", Qt::ISODate);
  performedAt->setDateTime(expectedUtc.toLocalTime());
  hand->setCurrentIndex(hand->findData(static_cast<int>(StrikeHand::Left)));
  count->setValue(144);
  duration->setValue(30);
  weapon->setCurrentIndex(
      weapon->findData(static_cast<int>(StrikeWeapon::Tyambara)));
  note->setPlainText(QString::fromUtf8("Контрольный замер"));

  const TimedStrikeTest result = dialog.test();
  return Check(result.isValid() && result.id == source.id &&
                   result.participantId == source.participantId &&
                   result.revision == source.revision &&
                   result.performedAt == expectedUtc &&
                   result.hand == StrikeHand::Left &&
                   result.strikeCount == 144 &&
                   result.durationSeconds == 30 &&
                   result.weapon == StrikeWeapon::Tyambara &&
                   result.note == QString::fromUtf8("Контрольный замер") &&
                   qAbs(result.strikesPerSecond() - 4.8) < 0.000001,
               "strike-test editor lost fields, UTC, or derived rate");
}

bool ParticipantStatisticsRequestsStrikeHistory()
{
  ParticipantStatisticsData statistics;
  ParticipantStatisticsWidget widget(statistics, std::nullopt);
  auto* button = widget.findChild<QPushButton*>(
      "participantStrikeHistoryButton");
  bool requested = false;
  QObject::connect(
      &widget, &ParticipantStatisticsWidget::strikeHistoryRequested, &widget,
      [&requested]() { requested = true; });
  if (button)
  {
    button->click();
  }
  return Check(button && requested,
               "strike-history button does not expose its request signal");
}

bool ParticipantCardKeepsUnchangedEmblem()
{
  ParticipantProfile profile;
  profile.id = {"12345678-1234-1234-1234-123456789abc"};
  profile.displayName = "Alice";
  profile.historicalName = "Alice";
  const ParticipantEmblem emblem = MakeParticipantEmblem(profile.id, 4);
  ParticipantStatisticsData statistics;
  ParticipantDialog dialog(profile, statistics, emblem, nullptr, true);

  const ParticipantCardUpdate update = dialog.cardUpdate();
  const auto* strikeHistoryButton = dialog.findChild<QPushButton*>(
      "participantStrikeHistoryButton");
  return Check(update.isValid() &&
                   update.emblemAction == ParticipantEmblemAction::Keep &&
                   !update.emblem.has_value() &&
                   update.expectedEmblemRevision == 0 &&
                   strikeHistoryButton && !strikeHistoryButton->isEnabled() &&
                   !strikeHistoryButton->toolTip().isEmpty(),
               "card without local storage exposes unavailable mutations");
}

bool ParticipantDirectoryHidesIdAndSortsByName()
{
  ParticipantProfile knight;
  knight.id = {"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"};
  knight.displayName = "Knight";
  knight.historicalName = "Knight";
  knight.fullName = "Knight Full Name";
  knight.rank = ParticipantRank::Knight;
  ParticipantProfile page;
  page.id = {"bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"};
  page.displayName = "Page";
  page.historicalName = "Page";
  page.rank = ParticipantRank::Page;
  ParticipantDirectoryDialog dialog({knight, page});
  auto* table = dialog.findChild<QTableWidget*>();
  if (!Check(table && table->columnCount() == 5,
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
                       QString::fromUtf8("Боевая рука") &&
                   table->horizontalHeaderItem(4)->text() ==
                       QString::fromUtf8("Статус"),
               "participant directory exposes ID or misses rank") &&
         Check(table->item(0, 0)->text() == "Knight" &&
                   table->item(1, 0)->text() == "Page",
               "participant directory is not sorted by display name") &&
         Check(table->item(0, 1)->text() == "Knight Full Name",
               "participant directory lost full name") &&
         Check(table->item(0, 0)->data(Qt::UserRole).toString() ==
                   knight.id.value,
               "participant directory lost hidden row identity");
}

bool EventEditorDoesNotDuplicateFullNameOnlyParticipant()
{
  ParticipantProfile profile;
  profile.id = {"33333333-3333-3333-3333-333333333333"};
  profile.displayName = "Анна Иванова";
  profile.fullName = "Анна Иванова";
  EventRecord event;
  event.id = CreateEventId();
  event.date = QDate(2026, 7, 18);
  EventDialog dialog(event, {profile});
  const auto* roster = dialog.findChild<QListWidget*>("eventParticipantsList");
  const auto* attendees = dialog.findChild<QListWidget*>(
      "eventNonCompetingAttendeesList");
  return Check(roster && attendees && roster->count() == 1 &&
                   attendees->count() == 1 &&
                   roster->item(0)->text() == QString::fromUtf8("Анна Иванова"),
               "full-name-only participant label is duplicated");
}

bool EventEditorSeparatesCompetitorsAndAttendees()
{
  ParticipantProfile profile;
  profile.id = {"33333333-3333-3333-3333-333333333333"};
  profile.displayName = "Анна";
  profile.historicalName = "Анна";
  EventRecord event;
  event.id = CreateEventId();
  event.title = "Выезд";
  event.date = QDate(2026, 7, 18);
  event.category = EventCategory::ExternalCompetition;
  EventDialog dialog(event, {profile});
  auto* competitors =
      dialog.findChild<QListWidget*>("eventParticipantsList");
  auto* attendees = dialog.findChild<QListWidget*>(
      "eventNonCompetingAttendeesList");
  if (!Check(competitors && attendees && competitors->count() == 1 &&
                 attendees->count() == 1,
             "event delegation role controls are missing"))
  {
    return false;
  }
  attendees->item(0)->setCheckState(Qt::Checked);
  EventRecord attendeeOnly = dialog.eventRecord();
  if (!Check(attendeeOnly.participants.empty() &&
                 attendeeOnly.nonCompetingAttendees.size() == 1 &&
                 attendeeOnly.isValid(),
             "non-competing attendee was added as competitor"))
  {
    return false;
  }
  competitors->item(0)->setCheckState(Qt::Checked);
  const EventRecord competitorOnly = dialog.eventRecord();
  return Check(competitorOnly.participants.size() == 1 &&
                   competitorOnly.nonCompetingAttendees.empty() &&
                   competitorOnly.isValid(),
               "competitor and attendee roles are not mutually exclusive");
}

bool EventEditorRequiresExplicitCategory()
{
  EventRecord event;
  event.id = CreateEventId();
  event.date = QDate(2026, 7, 18);
  EventDialog dialog(event, {});
  auto* category = dialog.findChild<QComboBox*>("eventCategoryCombo");
  if (!Check(category && category->count() == 4 &&
                 category->currentData().toInt() ==
                     static_cast<int>(EventCategory::Unspecified),
             "new event does not expose an explicit category choice"))
  {
    return false;
  }
  const int clubIndex = category->findData(
      static_cast<int>(EventCategory::ClubTrainingTournament));
  const int externalIndex = category->findData(
      static_cast<int>(EventCategory::ExternalCompetition));
  const int softCombatIndex = category->findData(
      static_cast<int>(EventCategory::SoftCombatTournament));
  if (!Check(clubIndex > 0 && externalIndex > 0 && softCombatIndex > 0 &&
                 category->itemText(clubIndex).contains(
                     QString::fromUtf8("Клубный")) &&
                 category->itemText(externalIndex).contains(
                     QString::fromUtf8("Внешние")) &&
                 category->itemText(softCombatIndex).contains(
                     QString::fromUtf8("СМБ")),
             "event category choices are incomplete"))
  {
    return false;
  }
  category->setCurrentIndex(clubIndex);
  const EventRecord classified = dialog.eventRecord();
  if (!Check(classified.category ==
                 EventCategory::ClubTrainingTournament,
             "event editor did not return selected category"))
  {
    return false;
  }

  EventRecord externalEvent = classified;
  externalEvent.category = EventCategory::ExternalCompetition;
  EventDialog reopened(externalEvent, {});
  const auto* reopenedCategory =
      reopened.findChild<QComboBox*>("eventCategoryCombo");
  return Check(reopenedCategory &&
                   reopenedCategory->currentData().toInt() ==
                       static_cast<int>(EventCategory::ExternalCompetition),
               "event editor did not restore persisted category");
}

bool EventEditorSupportsInternalAndFreeBoutSides()
{
  ParticipantProfile petya;
  petya.id = {"11111111-1111-1111-1111-111111111111"};
  petya.displayName = "Петя";
  petya.historicalName = "Петя";
  petya.fullName = "Пётр Петров";
  ParticipantProfile namesake;
  namesake.id = {"22222222-2222-2222-2222-222222222222"};
  namesake.displayName = "Петя";
  namesake.historicalName = "Петя";
  namesake.fullName = "Пётр Петров";
  EventRecord event;
  event.id = CreateEventId();
  event.date = QDate(2026, 7, 18);
  event.category = EventCategory::ClubTrainingTournament;
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
  if (!Check(sides.size() == 2 && scoreA.size() == 1 && scoreB.size() == 1 &&
                 removeButtons.size() == 1,
             "event bout controls missing"))
  {
    return false;
  }
  const auto* boutsTable = dialog.findChild<QTableWidget*>("eventBoutsTable");
  if (!Check(boutsTable && boutsTable->rowHeight(0) >= 44 &&
                 sides.at(0)->width() >= 160 && sides.at(1)->width() >= 160 &&
                 sides.at(0)->height() >= 30 && sides.at(1)->height() >= 30 &&
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
                 externalNamesake.bouts.front().sideB.freeName == internalLabel,
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

  petya.historicalName = "Петя после переименования";
  petya.displayName = petya.historicalName;
  petya.fullName = "Пётр После-Переименования";
  EventDialog reopened(edited, {petya});
  const EventRecord preserved = reopened.eventRecord();
  return Check(
      preserved.participants.size() == 1 &&
          preserved.category == EventCategory::ClubTrainingTournament &&
          preserved.participants.front().displayNameSnapshot == "Петя" &&
          preserved.participants.front().fullNameSnapshot == "Пётр Петров",
      "editing event silently rewrote historical name snapshot");
}

} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  if (!WeekdayClickSelectsAndClearsGroup() || !BulkControlsStaySynchronized() ||
      !AddParticipantsDialogUsesConfiguredMonthDropdown() ||
      !DayMarkerDialogSupportsMultipleKindsAndClear() ||
      !NoteOnlyMarkerBecomesOther() ||
      !AttendanceCellUsesCompactSemanticBadge() ||
      !TrainerMarkerUsesDedicatedBadge() ||
      !AttendanceCellHighlightsVisitAndSupportsContextMarkerEdit() ||
      !ParticipantStatisticsShowsAccessibleMonthHistory() ||
      !ParticipantAverageUsesOnlyCompletedMonths() ||
      !ParticipantMonthNavigationReturnsSelectionAndProtectsDirtyProfile() ||
      !ParticipantStatisticsDistinguishesUnavailableSources() ||
      !ParticipantEditorUsesReasonableYearAndKeepsIdInDetails() ||
      !ParticipantEditorRejectsFutureTrainingStart() ||
      !ParticipantEditorPreservesStoredFutureTrainingStart() ||
      !ParticipantEditorWarnsAboutStartAfterFirstRecordedVisit() ||
      !EmblemNormalizerScalesAndCanonicalizesImage() ||
      !EmblemNormalizerRejectsCorruptData() ||
      !EmblemNormalizerRejectsOversizedSource() ||
      !ReadOnlyMissingEmblemIsExplicitlyUnavailable() ||
      !ExistingParticipantEmblemCanBeDisplayedAndRemoved() ||
      !TimedStrikeEditorBuildsValidUtcRecord() ||
      !ParticipantStatisticsRequestsStrikeHistory() ||
      !ParticipantCardKeepsUnchangedEmblem() ||
      !ParticipantDirectoryHidesIdAndSortsByName() ||
      !EventEditorDoesNotDuplicateFullNameOnlyParticipant() ||
      !EventEditorSeparatesCompetitorsAndAttendees() ||
      !EventEditorRequiresExplicitCategory() ||
      !EventEditorSupportsInternalAndFreeBoutSides())
  {
    return 1;
  }
  return 0;
}
