#include "ParticipantDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

ParticipantDialog::ParticipantDialog(const ParticipantProfile& profile,
                                     bool editable, QWidget* parent)
    : QDialog(parent), original_(profile), action_(Action::Cancel),
      nameEdit_(new QLineEdit(profile.displayName, this)),
      birthdayCheck_(new QCheckBox("Дата рождения известна", this)),
      daySpin_(new QSpinBox(this)), monthSpin_(new QSpinBox(this)),
      yearSpin_(new QSpinBox(this)), notesEdit_(new QTextEdit(this)),
      saveButton_(nullptr), archiveButton_(nullptr), dirty_(false)
{
  setWindowTitle("Карточка участника");
  setMinimumWidth(460);

  daySpin_->setRange(1, 31);
  monthSpin_->setRange(1, 12);
  yearSpin_->setRange(0, 9999);
  yearSpin_->setSpecialValueText("Не указан");
  notesEdit_->setPlainText(profile.notes);
  notesEdit_->setAcceptRichText(false);

  if (profile.birthday.has_value())
  {
    birthdayCheck_->setChecked(true);
    daySpin_->setValue(profile.birthday->day);
    monthSpin_->setValue(profile.birthday->month);
    yearSpin_->setValue(profile.birthday->year.value_or(0));
  }

  auto* birthdayLayout = new QHBoxLayout();
  birthdayLayout->addWidget(new QLabel("День", this));
  birthdayLayout->addWidget(daySpin_);
  birthdayLayout->addWidget(new QLabel("Месяц", this));
  birthdayLayout->addWidget(monthSpin_);
  birthdayLayout->addWidget(new QLabel("Год", this));
  birthdayLayout->addWidget(yearSpin_);

  auto* form = new QFormLayout();
  form->addRow("Имя", nameEdit_);
  form->addRow(QString(), birthdayCheck_);
  form->addRow("Дата рождения", birthdayLayout);
  form->addRow("Заметка", notesEdit_);
  form->addRow("Статус",
               new QLabel(profile.archived ? "Архив" : "Активен", this));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  if (editable)
  {
    saveButton_ = buttons->addButton("Сохранить", QDialogButtonBox::AcceptRole);
    archiveButton_ =
        buttons->addButton(profile.archived ? "Восстановить" : "Архивировать",
                           QDialogButtonBox::ActionRole);
    connect(saveButton_, &QPushButton::clicked, this, [this]() { save(); });
    connect(archiveButton_, &QPushButton::clicked, this,
            [this]()
            {
              if (dirty_)
              {
                QMessageBox::warning(
                    this, "Несохраненные изменения",
                    "Сначала сохраните изменения профиля, затем повторно "
                    "откройте карточку для смены статуса.");
                return;
              }
              action_ = Action::ToggleArchive;
              accept();
            });
  }
  else
  {
    nameEdit_->setReadOnly(true);
    birthdayCheck_->setEnabled(false);
    notesEdit_->setReadOnly(true);
  }
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(nameEdit_, &QLineEdit::textChanged, this,
          [this]() { dirty_ = true; });
  connect(birthdayCheck_, &QCheckBox::toggled, this,
          [this]()
          {
            dirty_ = true;
            updateBirthdayControls();
          });
  connect(daySpin_, &QSpinBox::valueChanged, this, [this]() { dirty_ = true; });
  connect(monthSpin_, &QSpinBox::valueChanged, this,
          [this]() { dirty_ = true; });
  connect(yearSpin_, &QSpinBox::valueChanged, this,
          [this]() { dirty_ = true; });
  connect(notesEdit_, &QTextEdit::textChanged, this,
          [this]() { dirty_ = true; });

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
  updateBirthdayControls();
}

ParticipantDialog::Action ParticipantDialog::action() const
{
  return action_;
}

ParticipantProfile ParticipantDialog::profile() const
{
  ParticipantProfile result = original_;
  result.displayName = nameEdit_->text().trimmed();
  result.notes = notesEdit_->toPlainText();
  result.birthday = std::nullopt;
  if (birthdayCheck_->isChecked())
  {
    Birthday birthday{daySpin_->value(), monthSpin_->value(), std::nullopt};
    if (yearSpin_->value() != 0)
    {
      birthday.year = yearSpin_->value();
    }
    result.birthday = birthday;
  }
  return result;
}

bool ParticipantDialog::targetArchived() const
{
  return !original_.archived;
}

void ParticipantDialog::updateBirthdayControls()
{
  const bool enabled =
      birthdayCheck_->isChecked() && birthdayCheck_->isEnabled();
  daySpin_->setEnabled(enabled);
  monthSpin_->setEnabled(enabled);
  yearSpin_->setEnabled(enabled);
}

void ParticipantDialog::save()
{
  const ParticipantProfile edited = profile();
  if (!edited.isValid())
  {
    QMessageBox::warning(this, "Некорректный профиль",
                         "Проверьте имя, дату рождения и длину заметки (не "
                         "более 4096 символов).");
    return;
  }
  action_ = Action::Save;
  accept();
}
