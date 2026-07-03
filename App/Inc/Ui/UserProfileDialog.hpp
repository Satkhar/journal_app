#pragma once

#include <QDialog>

#include "IJournalStorage.hpp"

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class UserProfileDialog : public QDialog {
  Q_OBJECT

 public:
  explicit UserProfileDialog(const PersonProfile& profile, QWidget* parent = nullptr);

  PersonProfile profile() const;

 private:
  QLineEdit* nameEdit_;
  QSpinBox* ageSpinBox_;
  QLineEdit* profileUrlEdit_;
  QPlainTextEdit* notesEdit_;
};
