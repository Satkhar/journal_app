#include "UserProfileDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

UserProfileDialog::UserProfileDialog(const PersonProfile& profile, QWidget* parent)
    : QDialog(parent),
      nameEdit_(new QLineEdit(this)),
      ageSpinBox_(new QSpinBox(this)),
      profileUrlEdit_(new QLineEdit(this)),
      notesEdit_(new QPlainTextEdit(this)) {
  setWindowTitle("Карточка пользователя");

  nameEdit_->setText(profile.displayName);
  ageSpinBox_->setRange(0, 150);
  ageSpinBox_->setSpecialValueText("Не указан");
  ageSpinBox_->setValue(profile.age);
  profileUrlEdit_->setText(profile.profileUrl);
  notesEdit_->setPlainText(profile.notes);
  notesEdit_->setMinimumHeight(90);

  auto* formLayout = new QFormLayout();
  formLayout->addRow("Имя:", nameEdit_);
  formLayout->addRow("Возраст:", ageSpinBox_);
  formLayout->addRow("Профиль:", profileUrlEdit_);
  formLayout->addRow("Заметки:", notesEdit_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(formLayout);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (nameEdit_->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, "Карточка пользователя", "Имя не должно быть пустым.");
      return;
    }
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  setMinimumSize(sizeHint());
}

PersonProfile UserProfileDialog::profile() const {
  PersonProfile result;
  result.displayName = nameEdit_->text().trimmed();
  result.age = ageSpinBox_->value();
  result.profileUrl = profileUrlEdit_->text().trimmed();
  result.notes = notesEdit_->toPlainText().trimmed();
  return result;
}
