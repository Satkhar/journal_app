#include <iostream>
#include <QString>
#include <QTableWidget>
#include <QtSql>

// #include <QSqlTableModel>

// для работы с базой данных:
class DatabaseManager {
public:
  DatabaseManager(const QString &dbPath);
  bool createConnection();
  void loadTableData(QTableWidget *tableWidget);
  void writeTable(QTableWidget *tableWidget);
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