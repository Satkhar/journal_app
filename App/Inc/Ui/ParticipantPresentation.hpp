#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include <numeric>

#include "JournalModels.hpp"

inline QColor ParticipantRankBackgroundColor(ParticipantRank rank)
{
  switch (rank)
  {
  case ParticipantRank::Page:
    return QColor("#E7F0FF");
  case ParticipantRank::Squire:
    return QColor("#E7F6EC");
  case ParticipantRank::Novice:
    return QColor("#FFF4D8");
  case ParticipantRank::Recruit:
    return QColor("#F1E8FA");
  case ParticipantRank::Guest:
    return QColor("#F0F0F0");
  case ParticipantRank::Knight:
    return QColor("#FBE7E7");
  }
  return QColor("#F0F0F0");
}

inline QString CompactCombatHandName(CombatHand hand)
{
  switch (hand)
  {
  case CombatHand::Right:
    return "ПРАВ";
  case CombatHand::Left:
    return "ЛЕВ";
  case CombatHand::Unknown:
    return "—";
  }
  return "—";
}

inline double AverageAttendancePerTraining(
    const QVector<int>& attendanceByDay)
{
  if (attendanceByDay.isEmpty())
  {
    return 0.0;
  }
  const int total =
      std::accumulate(attendanceByDay.cbegin(), attendanceByDay.cend(), 0);
  return static_cast<double>(total) / attendanceByDay.size();
}
