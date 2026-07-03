#pragma once

#include <memory>

#include "IJournalStorage.hpp"
#include "SqliteConnect.hpp"

class JournalLocal : public IJournalStorage {
public:
  explicit JournalLocal(std::unique_ptr<SqliteConnect> sqlite);

  QStringList getUsersForMonth(int year, int month) override;
  QVector<int> getActiveDays(int year, int month) override;
  bool saveActiveDays(int year, int month, const QVector<int> &days) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord> &data) override;
  bool addUser(int year, int month, const QString &name) override;
  bool deleteUser(int year, int month, const QString &name) override;
  bool getPersonProfile(const QString& name, PersonProfile* profile) override;
  bool updatePersonProfile(const QString& originalName,
                           const PersonProfile& profile) override;

 private:
  // Владеем конкретным SQLite-адаптером и проксируем вызовы дальше.
  std::unique_ptr<SqliteConnect> sqlite_;
};
