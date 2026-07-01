#pragma once

#include <QString>

#include <vector>

#include "IJournalStorage.hpp"

class SyncService {
 public:
  // Это отдельный сервис синхронизации, чтобы UI не собирал сетевой сценарий вручную.
  // timeoutMs задает таймаут сетевых операций remote storage.
  explicit SyncService(int timeoutMs = 5000);

  // Push: берет уже собранный локальный срез месяца и отправляет на сервер.
  // Внутри открывается отдельный временный remote storage.
  bool pushMonthToServer(const QString& serverUrl, int year, int month,
                         const std::vector<AttendanceRecord>& localData,
                         QString* errorMessage = nullptr) const;

  // Pull: читает месяц с сервера и сохраняет его в переданное локальное хранилище.
  // Метод не создает local storage сам: это делает вызывающий код (UI/use-case).
  bool pullMonthToLocal(const QString& serverUrl, int year, int month,
                        IJournalStorage& localStorage,
                        QString* errorMessage = nullptr) const;

 private:
  int timeoutMs_;
};

