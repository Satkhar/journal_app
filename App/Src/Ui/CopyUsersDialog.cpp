#include "CopyUsersDialog.hpp"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLayout>
#include <QVBoxLayout>

CopyUsersDialog::CopyUsersDialog(int targetYear, int targetMonth,
                                 QWidget* parent)
    : QDialog(parent),
      calendar_(new QCalendarWidget(this)),
      copyActiveDaysCheckBox_(new QCheckBox("Также перенести дни учета", this)) {
  setWindowTitle("Перенести пользователей");

  const QDate targetDate(targetYear, targetMonth, 1);
  const QDate defaultSourceDate = targetDate.addMonths(-1);

  calendar_->setGridVisible(true);
  calendar_->setSelectedDate(defaultSourceDate);
  calendar_->setCurrentPage(defaultSourceDate.year(), defaultSourceDate.month());

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel("Выберите месяц-источник:", this));
  layout->addWidget(calendar_);
  layout->addWidget(copyActiveDaysCheckBox_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this);
  layout->addWidget(buttons);
  layout->setSizeConstraint(QLayout::SetMinimumSize);

  for (QAbstractButton* button : buttons->buttons()) {
    button->setMinimumWidth(button->sizeHint().width());
  }
  setMinimumSize(sizeHint());

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int CopyUsersDialog::sourceYear() const {
  return calendar_->selectedDate().year();
}

int CopyUsersDialog::sourceMonth() const {
  return calendar_->selectedDate().month();
}

bool CopyUsersDialog::copyActiveDays() const {
  return copyActiveDaysCheckBox_->isChecked();
}
