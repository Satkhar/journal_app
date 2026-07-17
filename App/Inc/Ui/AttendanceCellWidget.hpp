#pragma once

#include <QDate>
#include <QWidget>

#include <optional>

#include "JournalModels.hpp"

class QCheckBox;
class QToolButton;

class AttendanceCellWidget : public QWidget
{
  Q_OBJECT

public:
  AttendanceCellWidget(bool checked, QString participantName, QDate date,
                       std::optional<ParticipantDayMarker> marker,
                       QWidget* parent = nullptr);

  bool isChecked() const;
  QCheckBox* attendanceCheckBox() const;
  QToolButton* markerButton() const;
  const std::optional<ParticipantDayMarker>& marker() const;
  void setMarker(const std::optional<ParticipantDayMarker>& marker);
  void setEditable(bool editable);

private:
  QString participantName_;
  QDate date_;
  QCheckBox* attendanceCheckBox_;
  QToolButton* markerButton_;
  std::optional<ParticipantDayMarker> marker_;

  void updateMarkerPresentation();
};
