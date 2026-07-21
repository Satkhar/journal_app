#pragma once

#include <QWidget>

#include <optional>

#include "EventModels.hpp"
#include "JournalModels.hpp"

class QLabel;

struct ParticipantStatisticsData
{
  std::optional<ParticipantJournalStatistics> journal;
  QString journalError;
  std::optional<ParticipantEventStatistics> events;
  QString eventError;
};

class ParticipantStatisticsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ParticipantStatisticsWidget(
      const ParticipantStatisticsData& statistics,
      const std::optional<JournalMonth>& trainingStartMonth,
      QWidget* parent = nullptr);
  void setTrainingStartMonth(
      const std::optional<JournalMonth>& trainingStartMonth);

signals:
  void monthActivated(int year, int month);
  void strikeHistoryRequested();

private:
  QLabel* trainingStartLabel_;
  QLabel* trainingDurationLabel_;
  QLabel* trainingConsistencyWarning_;
  std::optional<QDate> firstRecordedVisit_;
};
