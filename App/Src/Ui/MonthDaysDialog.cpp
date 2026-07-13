#include "MonthDaysDialog.hpp"

#include <QCalendarWidget>
#include <QDate>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QVector<int> fullMonthDays(int year, int month) {
  QVector<int> days;
  const int maxDay = QDate(year, month, 1).daysInMonth();
  days.reserve(maxDay);
  for (int day = 1; day <= maxDay; ++day) {
    days.push_back(day);
  }
  return days;
}

QVector<int> sortedDays(const QSet<int>& selectedDays) {
  QVector<int> days;
  days.reserve(selectedDays.size());
  for (int day : selectedDays) {
    days.push_back(day);
  }
  std::sort(days.begin(), days.end());
  return days;
}

}  // namespace

MonthDaysDialog::MonthDaysDialog(int year, int month,
                                 const QVector<int>& activeDays,
                                 QWidget* parent)
    : QDialog(parent),
      year_(year),
      month_(month),
      calendar_(new QCalendarWidget(this)),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    this)) {
  setWindowTitle("Настроить месяц");

  for (int day : activeDays) {
    selectedDays_.insert(day);
  }
  if (selectedDays_.isEmpty()) {
    for (int day : fullMonthDays(year_, month_)) {
      selectedDays_.insert(day);
    }
  }

  calendar_->setGridVisible(true);
  calendar_->setMinimumDate(QDate(year_, month_, 1));
  calendar_->setMaximumDate(QDate(year_, month_, 1).addMonths(1).addDays(-1));
  calendar_->setSelectedDate(QDate(year_, month_, 1));
  calendar_->setCurrentPage(year_, month_);

  auto* selectAllButton = new QPushButton("Все дни", this);
  auto* clearButton = new QPushButton("Очистить", this);

  auto* actionsLayout = new QHBoxLayout();
  actionsLayout->addWidget(selectAllButton);
  actionsLayout->addWidget(clearButton);
  actionsLayout->addStretch();

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(calendar_);
  layout->addLayout(actionsLayout);
  layout->addWidget(buttons_);

  connect(calendar_, &QCalendarWidget::clicked, this, [this](const QDate& date) {
    if (date.year() != year_ || date.month() != month_) {
      return;
    }

    const int day = date.day();
    if (selectedDays_.contains(day)) {
      selectedDays_.remove(day);
    } else {
      selectedDays_.insert(day);
    }
    updateCalendarFormat();
  });

  connect(selectAllButton, &QPushButton::clicked, this, [this]() {
    selectedDays_.clear();
    for (int day : fullMonthDays(year_, month_)) {
      selectedDays_.insert(day);
    }
    updateCalendarFormat();
  });

  connect(clearButton, &QPushButton::clicked, this, [this]() {
    selectedDays_.clear();
    updateCalendarFormat();
  });

  connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
    if (selectedDays_.isEmpty()) {
      QMessageBox::warning(this, "Настроить месяц",
                           "Выберите хотя бы один день учета.");
      return;
    }
    accept();
  });
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  updateCalendarFormat();
}

QVector<int> MonthDaysDialog::selectedDays() const {
  return sortedDays(selectedDays_);
}

void MonthDaysDialog::updateCalendarFormat() {
  QTextCharFormat defaultFormat;
  QTextCharFormat selectedFormat;
  selectedFormat.setBackground(QColor("#cfe8ff"));
  selectedFormat.setForeground(Qt::black);

  for (int day : fullMonthDays(year_, month_)) {
    calendar_->setDateTextFormat(QDate(year_, month_, day), defaultFormat);
  }

  for (int day : selectedDays_) {
    calendar_->setDateTextFormat(QDate(year_, month_, day), selectedFormat);
  }
}
