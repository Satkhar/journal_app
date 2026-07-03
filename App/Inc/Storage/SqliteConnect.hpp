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
  QVector<int> getActiveDays(int year, int month);
  bool saveActiveDays(int year, int month, const QVector<int>& days);
  std::vector<AttendanceRecord> getMonth(int year, int month);
  bool saveMonth(int year, int month, const std::vector<AttendanceRecord>& data);
  bool addUser(int year, int month, const QString& name);
  bool deleteUser(int year, int month, const QString& name);
  bool getPersonProfile(const QString& name, PersonProfile* profile);
  bool updatePersonProfile(const QString& originalName, const PersonProfile& profile);

 private:
  // Qt хранит соединения в глобальном пуле по имени; db_ это handle на него.
  QSqlDatabase db_;
  QString connectionName_;

  // Вспомогательные функции формирования SQL-данных.
  bool ensureSchema();
  bool migrateLegacyUsers();
  int ensurePerson(const QString& name);
  int findPersonId(const QString& name);
  QVector<int> fullMonthDays(int year, int month) const;
  QVector<int> normalizeDays(int year, int month, const QVector<int>& days) const;
  QString monthPattern(int year, int month) const;
  QString dayString(int year, int month, int day) const;
  int daysInMonth(int year, int month) const;
};
