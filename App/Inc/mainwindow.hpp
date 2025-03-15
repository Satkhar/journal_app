#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QtSql>

#include "journal_app.h" // Подключаем сгенерированный файл

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

  // void UpdateWindow(QMainWindow *MainWindow);

private:
  Ui::MainWindow *ui;            // основной объект UI
  QSqlDatabase db;               // тут файл с базой
  QTableWidget *baseTableWidget; // указатель на основную таблицу
                                 //   QPushButton *btnAdd;
                                 //   QPushButton *btnDel;

  void updateTable();
  // void CreateBase(QMainWindow *MainWindow);
  void addUser(const QString &name, int age);
  void delUserById(int id);
  void delUserByName(const QString &name);


  bool createConnection();
  void loadTableData();
  void viewTableData();
  void createTable();
  void createCheckTable();
  void createDefaultTable();
};

#endif // MAINWINDOW_H