#pragma once

#include <iostream>
#include <QString>
#include <QTableWidget>
#include <QtSql>

// #include <QSqlTableModel>

// для работы с базой данных:
class DatabaseManager {
public:
  // Создает объект менеджера БД без активного соединения.
  DatabaseManager();
  // Закрывает ресурсы менеджера.
  ~DatabaseManager();
  // void setDatabase(const QString &dbPath);   
  // Открывает SQLite и создает схему при необходимости.
  bool createConnection(const QString &dbPath);
  // Проверяет, есть ли записи в таблице users.
  bool isDatabaseEmpty();
  // Загружает данные в UI-таблицу (устаревший путь).
  void loadTableData(QTableWidget *tableWidget);
  // Сохраняет данные UI-таблицы в БД (устаревший путь).
  void writeTable(QTableWidget *tableWidget);
  // Полностью очищает таблицу users.
  bool clearDB();

private:
  QSqlDatabase db;
};

// // для управления основной таблицей:
// class MainTableManager {
//   public:
//       MainTableManager(QTableWidget* tableWidget);
//       void createEmptyTable();
//       void updateTable();
//       void addUser(const QString &name);
//       void delUser(const QString &name);
//       int searchName(const QString &searched_name) const;
//       int searchDate(const QString &date_in_db) const;
//       void addCheckBox(int row, int column, bool is_checked);
      
//   private:
//       QTableWidget* tableWidget;
//       int month;
//       int year;
//       int day_in_month;
//   };
