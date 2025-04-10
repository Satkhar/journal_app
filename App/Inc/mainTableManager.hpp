#pragma once

#include <QString>
#include <QTableWidget>

// для управления основной таблицей:
class MainTableManager {
public:
  MainTableManager();
  ~MainTableManager();
  //   MainTableManager(QTableWidget* tableWidget);
  void setTableWidget(QTableWidget *tableWidget);
  void createEmptyTable();
  void updateTable();
  void addUser(const QString &name);
  void delUser(const QString &name);
  int searchName(const QString &searched_name) const;
  int searchDate(const QString &date_in_db) const;
  void addCheckBox(int row, int column, bool is_checked);

private:
  QTableWidget *tableWidget; // = nullptr;
  int month;
  int year;
  int day_in_month;
};