#pragma once

#include <QSqlDatabase>
#include <QString>

#include "IJournalStorage.hpp"

class SqliteConnect {
 public:
  // Создает объект без открытия соединения.
  SqliteConnect();
  // Закрывает соединение и освобождает зарегистрированное имя подключения.
  ~SqliteConnect();

  // Инициализация подключения и схемы БД.
  bool open(const QString& dbPath);

  // CRUD-операции на уровне месяца.
  QStringList getUsersForMonth(int year, int month);
  std::vector<AttendanceRecord> getMonth(int year, int month);
  bool saveMonth(int year, int month, const std::vector<AttendanceRecord>& data);
  bool addUser(int year, int month, const QString& name);
  bool deleteUser(int year, int month, const QString& name);

 private:
  QSqlDatabase db_;
  QString connectionName_;

  // Вспомогательные функции формирования SQL-данных.
  bool ensureSchema();
  QString monthPattern(int year, int month) const;
  QString dayString(int year, int month, int day) const;
  int daysInMonth(int year, int month) const;
};
