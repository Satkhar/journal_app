#pragma once

#include <QString>
#include <QStringList>

#include <vector>

struct AttendanceRecord {
  // Имя пользователя (ключ строки в таблице).
  QString userName;
  // День месяца [1..31].
  int day;
  // Отметка посещаемости за день.
  bool isChecked;
};

class IJournalStorage {
 public:
  virtual ~IJournalStorage() = default;

  // Возвращает список пользователей, у которых есть записи за месяц.
  virtual QStringList getUsersForMonth(int year, int month) = 0;
  // Возвращает все отметки пользователей за месяц.
  virtual std::vector<AttendanceRecord> getMonth(int year, int month) = 0;
  // Полностью сохраняет срез месяца.
  virtual bool saveMonth(int year, int month,
                         const std::vector<AttendanceRecord>& data) = 0;
  // Добавляет пользователя в месяц (как правило, заполняя все дни значением false).
  virtual bool addUser(int year, int month, const QString& name) = 0;
  // Удаляет пользователя из выбранного месяца.
  virtual bool deleteUser(int year, int month, const QString& name) = 0;
};
