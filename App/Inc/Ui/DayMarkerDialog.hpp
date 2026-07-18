#pragma once

#include <QDate>
#include <QDialog>

#include "JournalModels.hpp"

class QCheckBox;
class QLineEdit;

class DayMarkerDialog : public QDialog
{
public:
  DayMarkerDialog(const QString& participantName, const QDate& date,
                  DayMarkerKinds initialKinds, const QString& initialNote,
                  QWidget* parent = nullptr);

  DayMarkerKinds selectedKinds() const;
  QString note() const;
  bool clearRequested() const;

private:
  QCheckBox* paymentCheckBox_;
  QCheckBox* specialTrainingCheckBox_;
  QCheckBox* firstVisitCheckBox_;
  QCheckBox* otherCheckBox_;
  QCheckBox* trainerCheckBox_;
  QLineEdit* noteEdit_;
  bool clearRequested_;
};
