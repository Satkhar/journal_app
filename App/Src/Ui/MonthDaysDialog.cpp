#include "MonthDaysDialog.hpp"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

QVector<int> fullMonthDays(int year, int month)
{
  QVector<int> days;
  const int maxDay = QDate(year, month, 1).daysInMonth();
  days.reserve(maxDay);
  for (int day = 1; day <= maxDay; ++day)
  {
    days.push_back(day);
  }
  return days;
}

QVector<int> sortedDays(const QSet<int>& selectedDays)
{
  QVector<int> days;
  days.reserve(selectedDays.size());
  for (int day : selectedDays)
  {
    days.push_back(day);
  }
  std::sort(days.begin(), days.end());
  return days;
}

QVector<int> daysForWeekday(int year, int month, int dayOfWeek)
{
  QVector<int> days;
  for (int day : fullMonthDays(year, month))
  {
    if (QDate(year, month, day).dayOfWeek() == dayOfWeek)
    {
      days.push_back(day);
    }
  }
  return days;
}

} // namespace

MonthDaysDialog::MonthDaysDialog(int year, int month,
                                 const QVector<int>& activeDays,
                                 QWidget* parent)
    : QDialog(parent), year_(year), month_(month),
      calendar_(new QCalendarWidget(this)),
      buttons_(new QDialogButtonBox(
          QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)),
      weekdayCheckBoxes_{}
{
  setObjectName("monthDaysDialog");
  buttons_->setObjectName("monthDaysDialogButtons");
  setWindowTitle("Настроить месяц");

  for (int day : activeDays)
  {
    selectedDays_.insert(day);
  }
  if (selectedDays_.isEmpty())
  {
    for (int day : fullMonthDays(year_, month_))
    {
      selectedDays_.insert(day);
    }
  }

  calendar_->setGridVisible(true);
  calendar_->setFirstDayOfWeek(Qt::Monday);
  calendar_->setNavigationBarVisible(false);
  calendar_->setHorizontalHeaderFormat(QCalendarWidget::NoHorizontalHeader);
  calendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  calendar_->setMinimumDate(QDate(year_, month_, 1));
  calendar_->setMaximumDate(QDate(year_, month_, 1).addMonths(1).addDays(-1));
  calendar_->setSelectedDate(QDate(year_, month_, 1));
  calendar_->setCurrentPage(year_, month_);

  auto* selectAllButton = new QPushButton("Все дни", this);
  auto* clearButton = new QPushButton("Очистить", this);
  selectAllButton->setObjectName("selectAllDaysButton");
  clearButton->setObjectName("clearAllDaysButton");

  static constexpr std::array<const char*, 7> kWeekdayNames{
      "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
  auto* weekdaysLayout = new QHBoxLayout();
  for (int index = 0; index < static_cast<int>(weekdayCheckBoxes_.size());
       ++index)
  {
    auto* checkBox = new QCheckBox(kWeekdayNames.at(index), this);
    checkBox->setObjectName(
        QString("weekdayCheckBox%1").arg(index + 1));
    checkBox->setTristate(true);
    checkBox->setToolTip(
        "Выбрать все даты этого дня недели; повторное нажатие снимет их");
    weekdayCheckBoxes_.at(index) = checkBox;
    weekdaysLayout->addWidget(checkBox, 1, Qt::AlignCenter);
    connect(checkBox, &QCheckBox::clicked, this,
            [this, dayOfWeek = index + 1]()
            { toggleWeekday(dayOfWeek); });
  }

  auto* actionsLayout = new QHBoxLayout();
  actionsLayout->addWidget(selectAllButton);
  actionsLayout->addWidget(clearButton);
  actionsLayout->addStretch();

  auto* layout = new QVBoxLayout(this);
  auto* monthLabel =
      new QLabel(QString("Дни учета: %1.%2")
                     .arg(month_, 2, 10, QLatin1Char('0'))
                     .arg(year_),
                 this);
  monthLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(monthLabel);
  layout->addLayout(weekdaysLayout);
  layout->addWidget(calendar_);
  layout->addLayout(actionsLayout);
  layout->addWidget(buttons_);

  connect(calendar_, &QCalendarWidget::clicked, this,
          [this](const QDate& date)
          {
            if (date.year() != year_ || date.month() != month_)
            {
              return;
            }

            const int day = date.day();
            if (selectedDays_.contains(day))
            {
              selectedDays_.remove(day);
            }
            else
            {
              selectedDays_.insert(day);
            }
            updateCalendarFormat();
          });

  connect(selectAllButton, &QPushButton::clicked, this,
          [this]()
          {
            selectedDays_.clear();
            for (int day : fullMonthDays(year_, month_))
            {
              selectedDays_.insert(day);
            }
            updateCalendarFormat();
          });

  connect(clearButton, &QPushButton::clicked, this,
          [this]()
          {
            selectedDays_.clear();
            updateCalendarFormat();
          });

  connect(buttons_, &QDialogButtonBox::accepted, this,
          [this]()
          {
            if (selectedDays_.isEmpty())
            {
              QMessageBox::warning(this, "Настроить месяц",
                                   "Выберите хотя бы один день учета.");
              return;
            }
            accept();
          });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  updateCalendarFormat();
}

QVector<int> MonthDaysDialog::selectedDays() const
{
  return sortedDays(selectedDays_);
}

void MonthDaysDialog::toggleWeekday(int dayOfWeek)
{
  const QVector<int> weekdayDays =
      daysForWeekday(year_, month_, dayOfWeek);
  const bool allSelected =
      std::all_of(weekdayDays.cbegin(), weekdayDays.cend(),
                  [this](int day) { return selectedDays_.contains(day); });

  for (int day : weekdayDays)
  {
    if (allSelected)
    {
      selectedDays_.remove(day);
    }
    else
    {
      selectedDays_.insert(day);
    }
  }
  updateCalendarFormat();
}

void MonthDaysDialog::updateCalendarFormat()
{
  QTextCharFormat defaultFormat;
  QTextCharFormat selectedFormat;
  selectedFormat.setBackground(QColor("#cfe8ff"));
  selectedFormat.setForeground(Qt::black);

  for (int day : fullMonthDays(year_, month_))
  {
    calendar_->setDateTextFormat(QDate(year_, month_, day), defaultFormat);
  }

  for (int day : selectedDays_)
  {
    calendar_->setDateTextFormat(QDate(year_, month_, day), selectedFormat);
  }
  updateWeekdayControls();
}

void MonthDaysDialog::updateWeekdayControls()
{
  for (int index = 0; index < static_cast<int>(weekdayCheckBoxes_.size());
       ++index)
  {
    const QVector<int> weekdayDays =
        daysForWeekday(year_, month_, index + 1);
    const int selectedCount = static_cast<int>(std::count_if(
        weekdayDays.cbegin(), weekdayDays.cend(),
        [this](int day) { return selectedDays_.contains(day); }));

    Qt::CheckState state = Qt::PartiallyChecked;
    if (selectedCount == 0)
    {
      state = Qt::Unchecked;
    }
    else if (selectedCount == static_cast<int>(weekdayDays.size()))
    {
      state = Qt::Checked;
    }
    weekdayCheckBoxes_.at(index)->setCheckState(state);
  }
}
