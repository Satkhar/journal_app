#include "CopyUsersDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

CopyUsersDialog::CopyUsersDialog(int targetYear, int targetMonth,
                                 std::vector<JournalMonth> sourceMonths,
                                 QWidget* parent,
                                 bool copyWeekdayPatternByDefault)
    : QDialog(parent), sourceMonthCombo_(new QComboBox(this)),
      copyWeekdayPatternCheckBox_(
          new QCheckBox("Применить к целевому месяцу те же дни недели", this))
{
  setObjectName("copyUsersDialog");
  sourceMonthCombo_->setObjectName("copyUsersSourceMonthCombo");
  copyWeekdayPatternCheckBox_->setObjectName("copyWeekdayPatternCheckBox");
  copyWeekdayPatternCheckBox_->setToolTip(
      "Если опция включена, в целевом месяце будут выбраны все даты с "
      "теми же днями недели. Номера дат не копируются.");
  setWindowTitle("Добавление участников из месяца");

  const QDate targetDate(targetYear, targetMonth, 1);
  const QDate defaultSourceDate = targetDate.addMonths(-1);
  std::sort(sourceMonths.begin(), sourceMonths.end(),
            [](const JournalMonth& lhs, const JournalMonth& rhs)
            {
              return lhs.year != rhs.year ? lhs.year > rhs.year
                                          : lhs.month > rhs.month;
            });
  const QLocale russian(QLocale::Russian, QLocale::Russia);
  int defaultIndex = -1;
  int previousKey = -1;
  for (const JournalMonth& source : sourceMonths)
  {
    const QDate sourceDate(source.year, source.month, 1);
    const int key = source.year * 100 + source.month;
    if (!sourceDate.isValid() || sourceDate == targetDate || key == previousKey)
    {
      continue;
    }
    previousKey = key;
    QString label = russian.toString(sourceDate, "MMMM yyyy");
    if (!label.isEmpty())
    {
      label.replace(0, 1, label.left(1).toUpper());
    }
    sourceMonthCombo_->addItem(label, sourceDate);
    if (sourceDate == defaultSourceDate)
    {
      defaultIndex = sourceMonthCombo_->count() - 1;
    }
  }
  if (defaultIndex >= 0)
  {
    sourceMonthCombo_->setCurrentIndex(defaultIndex);
  }
  copyWeekdayPatternCheckBox_->setChecked(copyWeekdayPatternByDefault);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(
      new QLabel(QString("Целевой месяц: %1.%2")
                     .arg(targetMonth, 2, 10, QLatin1Char('0'))
                     .arg(targetYear),
                 this));
  layout->addWidget(new QLabel("Месяц-источник:", this));
  layout->addWidget(sourceMonthCombo_);
  layout->addWidget(copyWeekdayPatternCheckBox_);
  auto* weekdayHelp = new QLabel(
      "Участники добавляются к текущему составу; уже присутствующие не "
      "дублируются. Посещения и отметки текущего месяца сохраняются. Если "
      "опция включена, текущие дни учёта заменяются днями недели источника; "
      "номера дат и посещения источника не копируются.",
      this);
  weekdayHelp->setWordWrap(true);
  layout->addWidget(weekdayHelp);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName("copyUsersDialogButtons");
  buttons->button(QDialogButtonBox::Ok)
      ->setEnabled(sourceMonthCombo_->count() > 0);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int CopyUsersDialog::sourceYear() const
{
  return sourceMonthCombo_->currentData().toDate().year();
}

int CopyUsersDialog::sourceMonth() const
{
  return sourceMonthCombo_->currentData().toDate().month();
}

bool CopyUsersDialog::copyWeekdayPattern() const
{
  return copyWeekdayPatternCheckBox_->isChecked();
}
