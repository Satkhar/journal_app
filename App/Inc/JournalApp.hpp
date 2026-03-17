#pragma once

#include <memory>

#include "IJournalStorage.hpp"

struct MonthSnapshot {
  // Список строк таблицы (пользователи).
  QStringList users;
  // Отметки по дням для всех пользователей.
  std::vector<AttendanceRecord> attendance;
};

class JournalApp {
public:
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage,
                      bool allowBootstrapWrites = true);

  MonthSnapshot loadMonth(int year, int month);
  
  bool addUser(const QString &name);
  bool deleteUser(const QString &name);
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord> &data);

private:
  std::unique_ptr<IJournalStorage> storage_;
  bool allowBootstrapWrites_;
  int currentYear_;
  int currentMonth_;
};
