#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QString>

#include "IJournalStorage.hpp"

class JournalRemote : public IJournalStorage {
 public:
  explicit JournalRemote(const QString& baseUrl, int timeoutMs = 5000);

  bool connect(QString* errorMessage = nullptr);

  QStringList getUsersForMonth(int year, int month) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord>& data) override;
  bool addUser(int year, int month, const QString& name) override;
  bool deleteUser(int year, int month, const QString& name) override;

 private:
  QString baseUrl_;
  int timeoutMs_;
  QNetworkAccessManager network_;

  bool ensureSchema(QString* errorMessage = nullptr);
  QString monthPattern(int year, int month) const;
  QString dayString(int year, int month, int day) const;
  int daysInMonth(int year, int month) const;

  bool executePipeline(const QList<QString>& sqlStatements,
                       QJsonArray* outResults,
                       QString* errorMessage = nullptr);
  static QString sqlQuote(const QString& value);
};
