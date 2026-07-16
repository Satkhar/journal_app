#pragma once

#include <QSqlDatabase>
#include <QString>

#include "IJournalStorage.hpp"

class QSqlQuery;

class SqliteConnect {
 public:
  // Создает объект без открытия соединения.
  SqliteConnect();
  // Закрывает соединение и освобождает зарегистрированное имя подключения.
  ~SqliteConnect();

  // Инициализация подключения и схемы БД.
  bool open(const QString& dbPath);
  QString lastError() const;

  // CRUD-операции на уровне месяца.
  MonthStateResult getMonthState(int year, int month);
  QStringList getUsersForMonth(int year, int month);
  QVector<int> getActiveDays(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  std::vector<AttendanceRecord> getMonth(int year, int month);
  bool saveMonth(int year, int month, const std::vector<AttendanceRecord>& data);
  bool saveMonthSetup(int year, int month, const QVector<int>& days,
                      const std::vector<AttendanceRecord>& data);
  bool addUser(int year, int month, const QString& name);
  bool deleteUser(int year, int month, const QString& name);

 private:
  // Qt хранит соединения в глобальном пуле по имени; db_ это handle на него.
  QSqlDatabase db_;
  QString connectionName_;
  QString lastError_;

  // Вспомогательные функции формирования SQL-данных.
  bool ensureSchema();
  bool markMonthInitialized(int year, int month);
  bool commitTransaction(const char* operation);
  QVector<int> fullMonthDays(int year, int month) const;
  QVector<int> normalizeDays(int year, int month, const QVector<int>& days) const;
  QString monthPredicate() const;
  void bindMonth(QSqlQuery& query, int year, int month) const;
  QString monthPattern(int year, int month) const;
  QString legacyMonthPattern(int month) const;
  QString dayString(int year, int month, int day) const;
  QString legacyDayString(int month, int day) const;
  int daysInMonth(int year, int month) const;
};
