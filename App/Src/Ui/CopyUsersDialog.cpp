#include "CopyUsersDialog.hpp"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

CopyUsersDialog::CopyUsersDialog(int targetYear, int targetMonth,
                                 QWidget* parent,
                                 bool copyWeekdayPatternByDefault)
    : QDialog(parent), calendar_(new QCalendarWidget(this)),
      copyWeekdayPatternCheckBox_(
          new QCheckBox("Применить к целевому месяцу те же дни недели", this))
{
  setObjectName("copyUsersDialog");
  calendar_->setObjectName("copyUsersSourceCalendar");
  copyWeekdayPatternCheckBox_->setObjectName("copyWeekdayPatternCheckBox");
  copyWeekdayPatternCheckBox_->setToolTip(
      "Если опция включена, в целевом месяце будут выбраны все даты с "
      "теми же днями недели. Номера дат не копируются.");
  setWindowTitle("Перенос участников и расписания");

  const QDate targetDate(targetYear, targetMonth, 1);
  const QDate defaultSourceDate = targetDate.addMonths(-1);

  calendar_->setGridVisible(true);
  calendar_->setSelectionMode(QCalendarWidget::NoSelection);
  calendar_->setCurrentPage(defaultSourceDate.year(),
                            defaultSourceDate.month());
  copyWeekdayPatternCheckBox_->setChecked(copyWeekdayPatternByDefault);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(
      new QLabel(QString("Целевой месяц: %1.%2")
                     .arg(targetMonth, 2, 10, QLatin1Char('0'))
                     .arg(targetYear),
                 this));
  layout->addWidget(
      new QLabel("Выберите месяц-источник стрелками календаря:", this));
  layout->addWidget(calendar_);
  layout->addWidget(copyWeekdayPatternCheckBox_);
  auto* weekdayHelp = new QLabel(
      "Если опция включена, будут выбраны все даты с теми же днями недели. "
      "Числа месяца и отметки посещения источника не копируются; текущие "
      "дни целевого месяца заменяются.",
      this);
  weekdayHelp->setWordWrap(true);
  layout->addWidget(weekdayHelp);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName("copyUsersDialogButtons");
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int CopyUsersDialog::sourceYear() const
{
  return calendar_->yearShown();
}

int CopyUsersDialog::sourceMonth() const
{
  return calendar_->monthShown();
}

bool CopyUsersDialog::copyWeekdayPattern() const
{
  return copyWeekdayPatternCheckBox_->isChecked();
}
