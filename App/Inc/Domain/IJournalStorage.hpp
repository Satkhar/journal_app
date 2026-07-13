#pragma once

#include <QVector>

#include <vector>

#include "JournalModels.hpp"

class IJournalStorage
{
public:
  virtual ~IJournalStorage() = default;

  virtual std::vector<Participant> getParticipantsForMonth(int year,
                                                           int month) = 0;
  virtual QVector<int> getActiveDays(int year, int month) = 0;
  virtual bool saveActiveDays(int year, int month,
                              const QVector<int>& days) = 0;
  virtual std::vector<AttendanceRecord> getMonth(int year, int month) = 0;
  virtual bool saveAttendance(int year, int month,
                              const std::vector<AttendanceRecord>& data) = 0;
  virtual bool addParticipantToMonth(int year, int month,
                                     const Participant& participant) = 0;
  virtual bool removeParticipantFromMonth(int year, int month,
                                          const ParticipantId& id) = 0;
  virtual bool replaceMonth(int year, int month,
                            const MonthSnapshot& snapshot) = 0;
};
