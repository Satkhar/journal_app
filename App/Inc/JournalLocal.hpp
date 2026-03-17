#pragma once

#include <memory>

#include "IJournalStorage.hpp"
#include "SqliteConnect.hpp"

class JournalLocal : public IJournalStorage {
public:
  explicit JournalLocal(std::unique_ptr<SqliteConnect> sqlite);

  QStringList getUsersForMonth(int year, int month) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord> &data) override;
  bool addUser(int year, int month, const QString &name) override;
  bool deleteUser(int year, int month, const QString &name) override;

private:
  std::unique_ptr<SqliteConnect> sqlite_;
};
