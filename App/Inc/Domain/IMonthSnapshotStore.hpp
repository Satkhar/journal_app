#pragma once

#include <QString>

#include "JournalModels.hpp"

// Минимальная граница полной синхронизации месяца. Она намеренно не содержит
// CRUD профилей: серверный API сможет развиваться независимо от локального
// repository, не копируя весь IJournalStorage.
class IMonthSnapshotStore
{
public:
  virtual ~IMonthSnapshotStore() = default;

  // Snapshot читается как одна логическая версия. Реализация обязана вернуть
  // MonthState::Error вместо частично заполненных данных.
  virtual MonthSnapshot loadMonthSnapshot(int year, int month) = 0;

  // Замена должна быть атомарной: после ошибки прежний месяц остаётся целым.
  // До появления revision/CAS этот метод безопасен только для single-writer
  // PoC; несколько клиентов могут перезаписать изменения друг друга.
  virtual bool replaceMonth(int year, int month,
                            const MonthSnapshot& snapshot) = 0;

  // Временный синхронный error-channel. Его нельзя читать из другого потока;
  // async transport должен заменить его на Result<T, StorageError>.
  virtual QString lastError() const = 0;
};
