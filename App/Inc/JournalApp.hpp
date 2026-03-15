#pragma once

#include <memory>

#include "IJournalStorage.hpp"

struct MonthSnapshot {
  // Список строк таблицы (пользователи).
  QStringList users;
  // Отметки по дням для всех пользователей.
  std::vector<AttendanceRecord> attendance;
};

class JournalApp {
 public:
  // Это use-case слой между UI и конкретным storage.
  // UI не знает, SQLite это или remote HTTP-адаптер.
  // Инициализирует слой сценариев с конкретным хранилищем.
  // allowBootstrapWrites=true: разрешает стартовую инициализацию пустого месяца
  // (добавление Alice и первичное заполнение отметок).
  // allowBootstrapWrites=false: чтение месяца не должно ничего записывать.
  explicit JournalApp(std::unique_ptr<IJournalStorage> storage,
                      bool allowBootstrapWrites = true);

  // Читает состояние выбранного месяца и запоминает его как текущий.
  MonthSnapshot loadMonth(int year, int month);
  // Добавляет пользователя в текущий месяц.
  bool addUser(const QString& name);
  // Удаляет пользователя из текущего месяца.
  bool deleteUser(const QString& name);
  // Сохраняет состояние таблицы за указанный месяц.
  bool saveMonth(int year, int month, const std::vector<AttendanceRecord>& data);

 private:
  // Единственный активный storage для текущего экземпляра JournalApp.
  std::unique_ptr<IJournalStorage> storage_;
  // Флаг управляет старым MVP-поведением "автосидинг пустого месяца".
  bool allowBootstrapWrites_;
  // Эти поля запоминают последний открытый месяц, чтобы add/delete работали
  // без явной передачи year/month из каждого UI-обработчика.
  int currentYear_;
  int currentMonth_;
};
