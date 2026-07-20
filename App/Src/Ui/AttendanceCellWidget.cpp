#include "AttendanceCellWidget.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStringList>
#include <QToolButton>

#include <utility>

namespace
{

constexpr int kMarkerButtonSize = 20;

QStringList markerKindNames(DayMarkerKinds kinds)
{
  QStringList names;
  if (kinds.testFlag(DayMarkerKind::Payment))
  {
    names.push_back("Оплата");
  }
  if (kinds.testFlag(DayMarkerKind::SpecialTraining))
  {
    names.push_back("Тренировка в доспехах");
  }
  if (kinds.testFlag(DayMarkerKind::FirstVisit))
  {
    names.push_back("Первое посещение");
  }
  if (kinds.testFlag(DayMarkerKind::Other))
  {
    names.push_back("Другое");
  }
  if (kinds.testFlag(DayMarkerKind::LedTraining))
  {
    names.push_back("Вёл тренировку");
  }
  return names;
}

} // namespace

AttendanceCellWidget::AttendanceCellWidget(
    bool checked, QString participantName, QDate date,
    std::optional<ParticipantDayMarker> marker, QWidget* parent)
    : QWidget(parent), participantName_(std::move(participantName)),
      date_(std::move(date)),
      attendanceCheckBox_(new QCheckBox(this)),
      markerButton_(new QToolButton(this)), marker_(std::move(marker))
{
  setObjectName("attendanceCell");
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(1, 0, 1, 0);
  layout->setSpacing(1);
  layout->setAlignment(Qt::AlignCenter);

  attendanceCheckBox_->setObjectName("attendanceCheckBox");
  attendanceCheckBox_->setChecked(checked);
  attendanceCheckBox_->setAccessibleName(
      QString("Посещение: %1, %2")
          .arg(participantName_, date_.toString("dd.MM.yyyy")));
  layout->addWidget(attendanceCheckBox_);
  connect(attendanceCheckBox_, &QCheckBox::toggled, this,
          [this]() { updateAttendancePresentation(); });

  markerButton_->setObjectName("dayMarkerButton");
  markerButton_->setFixedSize(kMarkerButtonSize, kMarkerButtonSize);
  markerButton_->setFocusPolicy(Qt::StrongFocus);
  layout->addWidget(markerButton_);
  connect(markerButton_, &QToolButton::clicked, this,
          &AttendanceCellWidget::markerEditRequested);

  const auto requestMarkerEdit = [this](const QPoint&)
  {
    if (markerButton_->isEnabled())
    {
      emit markerEditRequested();
    }
  };
  setContextMenuPolicy(Qt::CustomContextMenu);
  attendanceCheckBox_->setContextMenuPolicy(Qt::CustomContextMenu);
  markerButton_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this,
          requestMarkerEdit);
  connect(attendanceCheckBox_, &QWidget::customContextMenuRequested, this,
          requestMarkerEdit);
  connect(markerButton_, &QWidget::customContextMenuRequested, this,
          requestMarkerEdit);

  updateAttendancePresentation();
  updateMarkerPresentation();
}

bool AttendanceCellWidget::isChecked() const
{
  return attendanceCheckBox_->isChecked();
}

QCheckBox* AttendanceCellWidget::attendanceCheckBox() const
{
  return attendanceCheckBox_;
}

QToolButton* AttendanceCellWidget::markerButton() const
{
  return markerButton_;
}

const std::optional<ParticipantDayMarker>& AttendanceCellWidget::marker() const
{
  return marker_;
}

void AttendanceCellWidget::setAttendanceChecked(bool checked)
{
  const QSignalBlocker blocker(attendanceCheckBox_);
  attendanceCheckBox_->setChecked(checked);
  updateAttendancePresentation();
}

void AttendanceCellWidget::setMarker(
    const std::optional<ParticipantDayMarker>& marker)
{
  marker_ = marker;
  updateMarkerPresentation();
}

void AttendanceCellWidget::setEditable(bool editable)
{
  attendanceCheckBox_->setEnabled(editable);
  markerButton_->setEnabled(editable);
}

void AttendanceCellWidget::updateAttendancePresentation()
{
  setAttribute(Qt::WA_StyledBackground, true);
  if (attendanceCheckBox_->isChecked())
  {
    setStyleSheet(
        "QWidget#attendanceCell { background: #DFF2DF; "
        "border: 1px solid #A6D2A6; }");
  }
  else
  {
    setStyleSheet(
        "QWidget#attendanceCell { background: transparent; border: 0; }");
  }
}

void AttendanceCellWidget::updateMarkerPresentation()
{
  if (!marker_.has_value())
  {
    markerButton_->setText("+");
    markerButton_->setStyleSheet(
        "QToolButton { color: #607D8B; background: #ECEFF1; "
        "border: 1px solid #B0BEC5; border-radius: 9px; font-weight: 700; }"
        "QToolButton:hover, QToolButton:focus { background: #CFD8DC; "
        "border-color: #90A4AE; }");
    const QString text =
        QString("Добавить отметку: %1, %2")
            .arg(participantName_, date_.toString("dd.MM.yyyy"));
    markerButton_->setToolTip(
        QString("<qt>%1</qt>").arg(text.toHtmlEscaped()));
    markerButton_->setAccessibleName(text);
    setToolTip(markerButton_->toolTip());
    setAccessibleName(text);
    return;
  }

  const QStringList names = markerKindNames(marker_->kinds);
  const int kindCount = CountDayMarkerKinds(marker_->kinds);
  QString symbol;
  QString color;
  if (kindCount > 1 &&
      marker_->kinds.testFlag(DayMarkerKind::LedTraining))
  {
    symbol = "Т+";
    color = "#6D4C41";
  }
  else if (kindCount > 1)
  {
    symbol = QString::number(kindCount);
    color = "#455A64";
  }
  else if (marker_->kinds.testFlag(DayMarkerKind::Payment))
  {
    symbol = "₽";
    color = "#2E7D32";
  }
  else if (marker_->kinds.testFlag(DayMarkerKind::SpecialTraining))
  {
    symbol = "★";
    color = "#6A1B9A";
  }
  else if (marker_->kinds.testFlag(DayMarkerKind::FirstVisit))
  {
    symbol = "1";
    color = "#1565C0";
  }
  else if (marker_->kinds.testFlag(DayMarkerKind::LedTraining))
  {
    symbol = "Т";
    color = "#8D4E1F";
  }
  else
  {
    symbol = "•";
    color = "#EF6C00";
  }

  markerButton_->setText(symbol);
  markerButton_->setStyleSheet(
      QString("QToolButton { color: white; background: %1; border: 0; "
              "border-radius: 9px; font-weight: 700; }")
          .arg(color));

  QString plainText =
      QString("%1, %2: %3")
          .arg(participantName_, date_.toString("dd.MM.yyyy"),
               names.join(", "));
  QString tooltip =
      QString("<qt><b>%1 — %2</b><br>%3")
          .arg(participantName_.toHtmlEscaped(),
               date_.toString("dd.MM.yyyy"),
               names.join(", ").toHtmlEscaped());
  if (!marker_->note.isEmpty())
  {
    plainText += ". " + marker_->note;
    tooltip += "<br>" + marker_->note.toHtmlEscaped();
  }
  tooltip += "</qt>";
  markerButton_->setToolTip(tooltip);
  markerButton_->setAccessibleName(plainText);
  setToolTip(tooltip);
  setAccessibleName(plainText);
}
