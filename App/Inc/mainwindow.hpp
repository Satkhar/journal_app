#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QTableWidget>
#include <QtSql>

#include "journal_app.h"  // Подключаем сгенерированный файл
#include "dbManager.hpp"
#include "checkTableManager.hpp"
#include "mainTableManager.hpp"

// Определяем список дней недели
const QStringList kDaysOfWeek = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вск"};

//----------------------------------------------------------------------------

class MainWindow : public QMainWindow
{
  Q_OBJECT

 public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

  // void UpdateWindow(QMainWindow *MainWindow);

 private:
  Ui::MainWindow *ui;             // основной объект UI

  DatabaseManager dbManager;
  MainTableManager mainTableManager;
  CheckTableManager checkTableManager;

  QSqlDatabase db;                // тут файл с базой
  QTableWidget *baseTableWidget;  // указатель на основную таблицу
                                  //   QPushButton *btnAdd;
                                  //   QPushButton *btnDel;
  uint32_t day_in_month;  // тут число дней для тек. месяца
  uint32_t month;   // установленный месяц
  uint32_t year;  // установленный год

  void updateTable();
  // void CreateBase(QMainWindow *MainWindow);
  void addUser(const QString &name, QTableWidget *tableWidget);
  void delUser(int id_to_del, QTableWidget *tableWidget);
  void delUser(const QString &name, QTableWidget *tableWidget);

  bool createConnection(const QString &dbPath);
  void loadTableData();
  void viewTableData();  // выводим db
  void createTable();
  void createCheckTable();   // таблица с маской требуемых дней
  void updToDefaultTable();  // создаем таблицу по умолчанию(пример)
  void createEmptyTable();   // создаем пустую таблицу
  void writeTable();         // записать из текущей таблицы в db
  bool clearDB(QSqlDatabase &db, const QString &tableName); // очистить от всех записей

  int searchDate(QTableWidget *tableWidget, QString date_in_db); // поиск столбца с нужной датой
  int searchName(QTableWidget *tableWidget, QString name); // поиск строки с нужным именем
  
  void addCheckBox(QTableWidget *tableWidget, int row, int column, bool is_checked); // добавление чекбокса
  void updateCalendarVariables(QCalendarWidget *calendarWidget);
};

#endif  // MAINWINDOW_H

//----------------------------------------------------------------------------