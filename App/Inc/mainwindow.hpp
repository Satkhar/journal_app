#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QtSql>


#include "journal_app.h" // Подключаем сгенерированный файл

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
  void addUser(const QString &name, int age);

  void UpdateWindow(QMainWindow *MainWindow);

private:
  Ui::MainWindow *ui;            // основной объект UI
  QSqlDatabase db;               // тут файл с базой
  QTableWidget *baseTableWidget; // указатель на основную таблицу
//   QPushButton *btnAdd;
//   QPushButton *btnDel;

  void MakeTable();
  void CreateBase(QMainWindow *MainWindow);

  bool createConnection();
  void loadTableData();
};

#endif // MAINWINDOW_H