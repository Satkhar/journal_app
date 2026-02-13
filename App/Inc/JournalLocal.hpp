#pragma once

#include <memory>

#include "IJournalStorage.hpp"
#include "SqliteConnect.hpp"

class JournalLocal : public IJournalStorage {
 public:
  // Локальная реализация хранилища через SQLite.
  explicit JournalLocal(std::unique_ptr<SqliteConnect> sqlite);

  // Возвращает список пользователей за выбранный месяц.
  QStringList getUsersForMonth(int year, int month) override;
  // Возвращает все отметки посещаемости за выбранный месяц.
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  // Полностью сохраняет состояние месяца.
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord>& data) override;
  // Добавляет пользователя на весь месяц.
  bool addUser(int year, int month, const QString& name) override;
  // Удаляет пользователя из выбранного месяца.
  bool deleteUser(int year, int month, const QString& name) override;

 private:
  std::unique_ptr<SqliteConnect> sqlite_;
};
