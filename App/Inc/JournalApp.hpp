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
  // Инициализирует слой сценариев с конкретным хранилищем.
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage);

  // Читает состояние выбранного месяца и запоминает его как текущий.
  MonthSnapshot loadMonth(int year, int month);
  // Добавляет пользователя в текущий месяц.
  bool addUser(const QString& name);
  // Удаляет пользователя из текущего месяца.
  bool deleteUser(const QString& name);
  // Сохраняет состояние таблицы за указанный месяц.
  bool saveMonth(int year, int month, const std::vector<AttendanceRecord>& data);

 private:
  std::unique_ptr<IJournalStorage> storage_;
  int currentYear_;
  int currentMonth_;
};
