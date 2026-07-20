#include "ParticipantStatisticsWidget.hpp"

#include <QDate>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

QString FormatDate(const std::optional<QDate>& date)
{
  return date.has_value() ? date->toString("dd.MM.yyyy") : QString("—");
}

QString CapitalizedMonthName(const QLocale& locale, int month,
                             QLocale::FormatType format);

QString FormatTrainingStartMonth(
    const std::optional<JournalMonth>& trainingStartMonth,
    const QLocale& locale)
{
  if (!trainingStartMonth.has_value())
  {
    return "Не указано";
  }
  const QDate start(trainingStartMonth->year, trainingStartMonth->month, 1);
  if (!start.isValid())
  {
    return "Некорректное значение";
  }
  return QString("%1 %2")
      .arg(CapitalizedMonthName(locale, start.month(), QLocale::LongFormat))
      .arg(start.year());
}

QString FormatTrainingDuration(
    const std::optional<JournalMonth>& trainingStartMonth)
{
  if (!trainingStartMonth.has_value())
  {
    return "—";
  }
  const QDate today = QDate::currentDate();
  const int totalMonths =
      (today.year() - trainingStartMonth->year) * 12 +
      today.month() - trainingStartMonth->month;
  if (totalMonths < 0)
  {
    return "Некорректное значение";
  }
  if (totalMonths == 0)
  {
    return "≈ менее месяца";
  }
  const int years = totalMonths / 12;
  const int months = totalMonths % 12;
  if (years == 0)
  {
    return QString("≈ %1 мес.").arg(months);
  }
  if (months == 0)
  {
    return QString("≈ %1 г.").arg(years);
  }
  return QString("≈ %1 г. %2 мес.").arg(years).arg(months);
}

QString CapitalizedMonthName(const QLocale& locale, int month,
                             QLocale::FormatType format)
{
  QString result = locale.standaloneMonthName(month, format);
  if (!result.isEmpty())
  {
    result.replace(0, 1, result.left(1).toUpper());
  }
  return result;
}

QLabel* AddMetric(QGridLayout* layout, int row, int column,
                  const QString& title, const QString& value,
                  const char* valueObjectName)
{
  auto* titleLabel = new QLabel(title);
  auto* valueLabel = new QLabel(value);
  valueLabel->setObjectName(valueObjectName);
  valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(titleLabel, row, column);
  layout->addWidget(valueLabel, row, column + 1);
  return valueLabel;
}

QString MonthCellStyle(const ParticipantMonthStatistics& statistics)
{
  const QString background =
      statistics.attendedDayCount > 0 ? "#DFF2DF" : "#ECEFF1";
  return QString(
             "QToolButton {"
             "  background: %1;"
             "  border: 1px solid #B0BEC5;"
             "  border-radius: 4px;"
             "  padding: 3px;"
             "}"
             "QToolButton:hover { border-color: #546E7A; }"
             "QToolButton:focus { border: 2px solid #455A64; }")
      .arg(background);
}

QString MonthToolTip(const ParticipantMonthStatistics& statistics,
                     const QLocale& locale)
{
  const QString monthName = CapitalizedMonthName(
      locale, statistics.month.month, QLocale::LongFormat);
  return QString("%1 %2\n"
                 "Посещено: %3\n"
                 "Дат учёта в месяце состава: %4\n"
                 "Тренировки в доспехах: %5\n"
                 "Проведено тренировок: %6\n"
                 "Нажмите, чтобы открыть месяц")
      .arg(monthName)
      .arg(statistics.month.year)
      .arg(statistics.attendedDayCount)
      .arg(statistics.trackedDayCount)
      .arg(statistics.specialTrainingVisitCount)
      .arg(statistics.ledTrainingDayCount);
}

QString MonthAccessibleName(const ParticipantMonthStatistics& statistics,
                            const QLocale& locale)
{
  const QString monthName = CapitalizedMonthName(
      locale, statistics.month.month, QLocale::LongFormat);
  return QString("%1 %2. Посещено: %3. "
                 "Дат учёта в месяце состава: %4. "
                 "Тренировок в доспехах: %5. "
                 "Проведено тренировок: %6. Открыть месяц.")
      .arg(monthName)
      .arg(statistics.month.year)
      .arg(statistics.attendedDayCount)
      .arg(statistics.trackedDayCount)
      .arg(statistics.specialTrainingVisitCount)
      .arg(statistics.ledTrainingDayCount);
}

QString CompletedMonthAverage(
    const ParticipantJournalStatistics& statistics, const QLocale& locale)
{
  const QDate today = QDate::currentDate();
  const QDate currentMonth(today.year(), today.month(), 1);
  int completedMonthCount = 0;
  qint64 completedMonthAttendance = 0;
  for (const ParticipantMonthStatistics& month : statistics.months)
  {
    const QDate monthDate(month.month.year, month.month.month, 1);
    if (monthDate.isValid() && monthDate < currentMonth)
    {
      ++completedMonthCount;
      completedMonthAttendance += month.attendedDayCount;
    }
  }
  if (completedMonthCount == 0)
  {
    return "—";
  }
  const double average =
      static_cast<double>(completedMonthAttendance) / completedMonthCount;
  return locale.toString(average, 'f', 1);
}

} // namespace

ParticipantStatisticsWidget::ParticipantStatisticsWidget(
    const ParticipantStatisticsData& statistics,
    const std::optional<JournalMonth>& trainingStartMonth, QWidget* parent)
    : QWidget(parent), trainingStartLabel_(nullptr),
      trainingDurationLabel_(nullptr), trainingConsistencyWarning_(nullptr),
      firstRecordedVisit_(statistics.journal.has_value()
                              ? statistics.journal->firstRecordedVisit
                              : std::optional<QDate>())
{
  setObjectName("participantStatisticsWidget");

  auto* rootLayout = new QVBoxLayout(this);

  const QLocale russian(QLocale::Russian, QLocale::Russia);
  auto* experienceGroup = new QGroupBox("Стаж", this);
  auto* experienceLayout = new QGridLayout(experienceGroup);
  trainingStartLabel_ =
      AddMetric(experienceLayout, 0, 0, "Начало тренировок", QString(),
                "participantTrainingStartLabel");
  trainingDurationLabel_ =
      AddMetric(experienceLayout, 1, 0, "Примерный стаж", QString(),
                "participantTrainingDurationLabel");
  auto* experienceHint = new QLabel(
      "Начало тренировок задаётся вручную с точностью до месяца. Стаж "
      "рассчитывается приблизительно и не изменяет счётчики журнала.",
      experienceGroup);
  experienceHint->setWordWrap(true);
  experienceLayout->addWidget(experienceHint, 2, 0, 1, 2);
  trainingConsistencyWarning_ = new QLabel(experienceGroup);
  trainingConsistencyWarning_->setObjectName(
      "participantTrainingStartConsistencyWarning");
  trainingConsistencyWarning_->setWordWrap(true);
  trainingConsistencyWarning_->setStyleSheet("color: #9A5A00;");
  experienceLayout->addWidget(trainingConsistencyWarning_, 3, 0, 1, 2);
  setTrainingStartMonth(trainingStartMonth);
  experienceLayout->setColumnStretch(1, 1);
  rootLayout->addWidget(experienceGroup);

  auto* journalGroup = new QGroupBox("Журнал", this);
  auto* journalLayout = new QGridLayout(journalGroup);
  if (statistics.journal.has_value())
  {
    AddMetric(journalLayout, 0, 0, "Посещений по журналу",
              QString::number(statistics.journal->totalAttendedDayCount),
              "participantTotalAttendanceLabel");
    AddMetric(
        journalLayout, 1, 0, "Посещено тренировок в доспехах",
        QString::number(
            statistics.journal->totalSpecialTrainingVisitCount),
        "participantSpecialTrainingCountLabel");
    AddMetric(journalLayout, 2, 0, "Провёл тренировок",
              QString::number(statistics.journal->totalLedTrainingDayCount),
              "participantLedTrainingCountLabel");
    AddMetric(journalLayout, 3, 0,
              "Среднее за завершённый месяц в составе",
              CompletedMonthAverage(*statistics.journal, russian),
              "participantAverageAttendanceLabel");
    AddMetric(journalLayout, 0, 2, "Первое посещение в журнале",
              FormatDate(statistics.journal->firstRecordedVisit),
              "participantFirstVisitLabel");
    AddMetric(journalLayout, 1, 2, "Последнее посещение в журнале",
              FormatDate(statistics.journal->lastRecordedVisit),
              "participantLastVisitLabel");
    journalLayout->setColumnStretch(1, 1);
    journalLayout->setColumnStretch(3, 1);
  }
  else
  {
    const QString error = statistics.journalError.trimmed();
    auto* unavailable = new QLabel(
        error.isEmpty() ? "Статистика журнала недоступна"
                        : QString("Статистика журнала недоступна: %1")
                              .arg(error),
        this);
    unavailable->setObjectName("participantJournalStatisticsUnavailableLabel");
    unavailable->setWordWrap(true);
    journalLayout->addWidget(unavailable, 0, 0);
  }
  rootLayout->addWidget(journalGroup);

  auto* eventGroup = new QGroupBox("Турниры", this);
  auto* eventLayout = new QGridLayout(eventGroup);
  if (statistics.events.has_value())
  {
    AddMetric(eventLayout, 0, 0, "Турниров",
              QString::number(statistics.events->tournamentCount),
              "participantTournamentCountLabel");
    AddMetric(eventLayout, 1, 0, "Клубных турниров на тренировке",
              QString::number(statistics.events->clubTournamentCount),
              "participantClubTournamentCountLabel");
    AddMetric(eventLayout, 2, 0, "Выездных соревнований",
              QString::number(statistics.events->externalCompetitionCount),
              "participantExternalCompetitionCountLabel");
    AddMetric(eventLayout, 3, 0, "СМБ-турниров (мягких)",
              QString::number(statistics.events->softCombatTournamentCount),
              "participantSoftCombatTournamentCountLabel");
    AddMetric(eventLayout, 4, 0, "Турниров без категории",
              QString::number(statistics.events->unspecifiedTournamentCount),
              "participantUnspecifiedTournamentCountLabel");
    AddMetric(eventLayout, 5, 0, "Выездов без участия в соревнованиях",
              QString::number(statistics.events->nonCompetingTripCount),
              "participantNonCompetingTripCountLabel");
    AddMetric(eventLayout, 6, 0, "Боёв",
              QString::number(statistics.events->boutCount),
              "participantBoutCountLabel");
    AddMetric(eventLayout, 0, 2, "Первый турнир",
              FormatDate(statistics.events->firstTournament),
              "participantFirstTournamentLabel");
    AddMetric(eventLayout, 1, 2, "Последний турнир",
              FormatDate(statistics.events->lastTournament),
              "participantLastTournamentLabel");
    eventLayout->setColumnStretch(1, 1);
    eventLayout->setColumnStretch(3, 1);
  }
  else
  {
    const QString error = statistics.eventError.trimmed();
    auto* unavailable = new QLabel(
        error.isEmpty() ? "Статистика турниров недоступна"
                        : QString("Статистика турниров недоступна: %1")
                              .arg(error),
        this);
    unavailable->setObjectName("participantEventStatisticsUnavailableLabel");
    unavailable->setWordWrap(true);
    eventLayout->addWidget(unavailable, 0, 0);
  }
  rootLayout->addWidget(eventGroup);

  auto* timelineGroup = new QGroupBox("Активность по месяцам", this);
  auto* timelineLayout = new QVBoxLayout(timelineGroup);
  if (!statistics.journal.has_value() || statistics.journal->months.empty())
  {
    auto* empty = new QLabel("Нет данных по месяцам", this);
    empty->setObjectName("participantMonthStatisticsEmptyLabel");
    timelineLayout->addWidget(empty);
  }
  else
  {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("participantMonthStatisticsScrollArea");
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scrollArea);
    content->setObjectName("participantMonthStatisticsContent");
    auto* grid = new QGridLayout(content);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);

    auto* yearHeader = new QLabel("Год", content);
    yearHeader->setAlignment(Qt::AlignCenter);
    grid->addWidget(yearHeader, 0, 0);
    for (int month = 1; month <= 12; ++month)
    {
      auto* header = new QLabel(CapitalizedMonthName(
          russian, month, QLocale::ShortFormat), content);
      header->setAlignment(Qt::AlignCenter);
      grid->addWidget(header, 0, month);
    }

    auto months = statistics.journal->months;
    std::sort(months.begin(), months.end(),
              [](const ParticipantMonthStatistics& lhs,
                 const ParticipantMonthStatistics& rhs)
              {
                return lhs.month.year != rhs.month.year
                           ? lhs.month.year < rhs.month.year
                           : lhs.month.month < rhs.month.month;
              });

    int currentYear = -1;
    int row = 0;
    for (const ParticipantMonthStatistics& statistics : months)
    {
      const QDate monthDate(statistics.month.year, statistics.month.month, 1);
      if (!monthDate.isValid())
      {
        continue;
      }
      if (statistics.month.year != currentYear)
      {
        currentYear = statistics.month.year;
        ++row;
        auto* yearLabel =
            new QLabel(QString::number(statistics.month.year), content);
        yearLabel->setAlignment(Qt::AlignCenter);
        grid->addWidget(yearLabel, row, 0);
      }

      auto* button = new QToolButton(content);
      button->setObjectName(
          QString("participantMonthButton_%1_%2")
              .arg(statistics.month.year)
              .arg(statistics.month.month, 2, 10, QLatin1Char('0')));
      button->setProperty("year", statistics.month.year);
      button->setProperty("month", statistics.month.month);
      button->setText(
          QString("%1\n%2/%3")
              .arg(CapitalizedMonthName(russian, statistics.month.month,
                                        QLocale::ShortFormat))
              .arg(statistics.attendedDayCount)
              .arg(statistics.trackedDayCount));
      button->setToolTip(MonthToolTip(statistics, russian));
      button->setAccessibleName(MonthAccessibleName(statistics, russian));
      button->setAccessibleDescription(
          "Открывает выбранный месяц в основном журнале");
      button->setMinimumSize(58, 44);
      button->setFocusPolicy(Qt::StrongFocus);
      button->setStyleSheet(MonthCellStyle(statistics));
      const int targetYear = statistics.month.year;
      const int targetMonth = statistics.month.month;
      connect(button, &QToolButton::clicked, this,
              [this, targetYear, targetMonth]()
              { emit monthActivated(targetYear, targetMonth); });
      grid->addWidget(button, row, statistics.month.month);
    }
    grid->setColumnStretch(13, 1);
    grid->setRowStretch(row + 1, 1);
    scrollArea->setWidget(content);
    timelineLayout->addWidget(scrollArea);
  }
  rootLayout->addWidget(timelineGroup, 1);
}

void ParticipantStatisticsWidget::setTrainingStartMonth(
    const std::optional<JournalMonth>& trainingStartMonth)
{
  const QLocale russian(QLocale::Russian, QLocale::Russia);
  trainingStartLabel_->setText(
      FormatTrainingStartMonth(trainingStartMonth, russian));
  trainingDurationLabel_->setText(
      FormatTrainingDuration(trainingStartMonth));
  bool contradictsJournal = false;
  if (trainingStartMonth.has_value() && firstRecordedVisit_.has_value())
  {
    const QDate start(trainingStartMonth->year,
                      trainingStartMonth->month, 1);
    const QDate firstVisitMonth(firstRecordedVisit_->year(),
                                firstRecordedVisit_->month(), 1);
    contradictsJournal = start.isValid() && start > firstVisitMonth;
  }
  trainingConsistencyWarning_->setVisible(contradictsJournal);
  trainingConsistencyWarning_->setText(
      contradictsJournal
          ? QString("Проверьте начало тренировок: первое посещение в журнале "
                    "записано раньше — %1.")
                .arg(firstRecordedVisit_->toString("dd.MM.yyyy"))
          : QString());
}
