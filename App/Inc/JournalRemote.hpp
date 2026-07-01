#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QString>

#include "IJournalStorage.hpp"

class JournalRemote : public IJournalStorage {
 public:
  // baseUrl ожидается в формате http://host:port (например http://127.0.0.1:7070).
  explicit JournalRemote(const QString& baseUrl, int timeoutMs = 5000);

  // Проверка доступности сервера + ensureSchema.
  bool connect(QString* errorMessage = nullptr);
  // Последняя ошибка операций remote storage (чтение/запись/парсинг/сеть).
  QString lastError() const;

  QStringList getUsersForMonth(int year, int month) override;
  std::vector<AttendanceRecord> getMonth(int year, int month) override;
  bool saveMonth(int year, int month,
                 const std::vector<AttendanceRecord> &data) override;
  bool addUser(int year, int month, const QString &name) override;
  bool deleteUser(int year, int month, const QString &name) override;

 private:
  // Базовый URL сервера libsql/http endpoint.
  QString baseUrl_;
  // Таймаут локального event loop ожидания ответа.
  int timeoutMs_;
  // Последняя диагностическая ошибка для pull/read сценариев.
  QString lastError_;
  QNetworkAccessManager network_;

  // Создает таблицу users, если она отсутствует на сервере.
  bool ensureSchema(QString* errorMessage = nullptr);
  // Формат month/day для SQL-фильтрации и вставки.
  QString monthPattern(int year, int month) const;
  QString dayString(int year, int month, int day) const;
  int daysInMonth(int year, int month) const;

  // Единая точка общения с libsql /v2/pipeline:
  // - формирует JSON requests[]
  // - отправляет POST
  // - парсит JSON results[]
  // - возвращает ошибку в errorMessage
  bool executePipeline(const QList<QString>& sqlStatements,
                       QJsonArray* outResults,
                       QString* errorMessage = nullptr);
  // Минимальное экранирование строк для SQL литералов.
  static QString sqlQuote(const QString& value);
};
