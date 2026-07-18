#include "DayMarkerDialog.hpp"

#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{

constexpr int kDialogMinimumWidth = 390;

} // namespace

DayMarkerDialog::DayMarkerDialog(const QString& participantName,
                                 const QDate& date,
                                 DayMarkerKinds initialKinds,
                                 const QString& initialNote, QWidget* parent)
    : QDialog(parent), paymentCheckBox_(new QCheckBox("Оплата", this)),
      specialTrainingCheckBox_(
          new QCheckBox("Особенная тренировка", this)),
      firstVisitCheckBox_(new QCheckBox("Первое посещение", this)),
      otherCheckBox_(new QCheckBox("Другое", this)),
      trainerCheckBox_(new QCheckBox("Вёл тренировку", this)),
      noteEdit_(new QLineEdit(this)), clearRequested_(false)
{
  setWindowTitle("Отметка дня");
  setMinimumWidth(kDialogMinimumWidth);

  auto* layout = new QVBoxLayout(this);
  auto* title = new QLabel(
      QString("%1 — %2")
          .arg(participantName.toHtmlEscaped(), date.toString("dd.MM.yyyy")),
      this);
  title->setTextFormat(Qt::RichText);
  title->setWordWrap(true);
  layout->addWidget(title);

  auto* hint = new QLabel(
      "Можно выбрать несколько событий. Цвет и значок появятся в таблице.",
      this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  paymentCheckBox_->setObjectName("paymentMarkerCheckBox");
  specialTrainingCheckBox_->setObjectName(
      "specialTrainingMarkerCheckBox");
  firstVisitCheckBox_->setObjectName("firstVisitMarkerCheckBox");
  otherCheckBox_->setObjectName("otherMarkerCheckBox");
  trainerCheckBox_->setObjectName("trainerMarkerCheckBox");
  paymentCheckBox_->setChecked(initialKinds.testFlag(DayMarkerKind::Payment));
  specialTrainingCheckBox_->setChecked(
      initialKinds.testFlag(DayMarkerKind::SpecialTraining));
  firstVisitCheckBox_->setChecked(
      initialKinds.testFlag(DayMarkerKind::FirstVisit));
  otherCheckBox_->setChecked(initialKinds.testFlag(DayMarkerKind::Other));
  trainerCheckBox_->setChecked(
      initialKinds.testFlag(DayMarkerKind::LedTraining));
  layout->addWidget(paymentCheckBox_);
  layout->addWidget(specialTrainingCheckBox_);
  layout->addWidget(firstVisitCheckBox_);
  layout->addWidget(otherCheckBox_);
  layout->addWidget(trainerCheckBox_);

  noteEdit_->setObjectName("dayMarkerNoteEdit");
  noteEdit_->setMaxLength(kMaxDayMarkerNoteLength);
  noteEdit_->setText(initialNote);
  noteEdit_->setPlaceholderText("Короткий комментарий");
  layout->addWidget(new QLabel("Комментарий:", this));
  layout->addWidget(noteEdit_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  auto* clearButton =
      buttons->addButton("Очистить", QDialogButtonBox::DestructiveRole);
  clearButton->setObjectName("clearDayMarkerButton");
  clearButton->setEnabled(initialKinds.toInt() != 0 ||
                          !initialNote.isEmpty());
  buttons->button(QDialogButtonBox::Save)->setText("Сохранить");
  buttons->button(QDialogButtonBox::Save)
      ->setObjectName("saveDayMarkerButton");
  buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this,
          [this]()
          {
            if (selectedKinds().toInt() == 0 && !note().isEmpty())
            {
              otherCheckBox_->setChecked(true);
            }
            clearRequested_ =
                selectedKinds().toInt() == 0 && note().isEmpty();
            accept();
          });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(clearButton, &QPushButton::clicked, this,
          [this]()
          {
            clearRequested_ = true;
            accept();
          });
}

DayMarkerKinds DayMarkerDialog::selectedKinds() const
{
  DayMarkerKinds kinds;
  if (paymentCheckBox_->isChecked())
  {
    kinds |= DayMarkerKind::Payment;
  }
  if (specialTrainingCheckBox_->isChecked())
  {
    kinds |= DayMarkerKind::SpecialTraining;
  }
  if (firstVisitCheckBox_->isChecked())
  {
    kinds |= DayMarkerKind::FirstVisit;
  }
  if (otherCheckBox_->isChecked())
  {
    kinds |= DayMarkerKind::Other;
  }
  if (trainerCheckBox_->isChecked())
  {
    kinds |= DayMarkerKind::LedTraining;
  }
  return kinds;
}

QString DayMarkerDialog::note() const
{
  return noteEdit_->text().trimmed();
}

bool DayMarkerDialog::clearRequested() const
{
  return clearRequested_;
}
