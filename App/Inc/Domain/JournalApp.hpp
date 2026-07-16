#pragma once

#include <memory>
#include <QVector>

#include "IJournalStorage.hpp"

struct MonthSnapshot {
  MonthState state{MonthState::Error};
  QString errorMessage;
  // Список строк таблицы (пользователи).
  QStringList users;
  // Дни месяца, которые входят в учет посещаемости.
  QVector<int> activeDays;
  // Отметки по дням для всех пользователей.
  std::vector<AttendanceRecord> attendance;
};

struct CopyUsersResult {
  bool ok;
  int copied;
  int skipped;
  QString errorMessage;
};

class JournalApp {
public:
  // Это use-case слой между UI и конкретным storage.
  // UI не знает, SQLite это или remote HTTP-адаптер.
  // Инициализирует слой сценариев с конкретным хранилищем.
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage);

  MonthStateResult getMonthState(int year, int month);
  MonthSnapshot loadMonth(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  CopyUsersResult copyUsersFromMonth(int fromYear, int fromMonth, int toYear,
                                     int toMonth, bool copyActiveDays);
  
  bool addUser(const QString &name);
  bool deleteUser(const QString &name);
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord> &data);

 private:
  // Единственный активный storage для текущего экземпляра JournalApp.
  std::unique_ptr<IJournalStorage> storage_;
  // Эти поля запоминают последний открытый месяц, чтобы add/delete работали
  // без явной передачи year/month из каждого UI-обработчика.
  int currentYear_;
  int currentMonth_;
};
