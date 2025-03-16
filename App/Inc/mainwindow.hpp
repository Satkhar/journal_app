#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QtSql>
#include <QStringList>

#include "journal_app.h" // Подключаем сгенерированный файл

// Определяем список дней недели
const QStringList kDaysOfWeek = {
  "Пн",
  "Вт",
  "Ср",
  "Чт",
  "Пт",
  "Сб",
  "Вск"
};


//----------------------------------------------------------------------------

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
  void addUser(const QString &name);
  void delUserById(int id);
  void delUserByName(const QString &name);


  bool createConnection();
  void loadTableData();
  void viewTableData(); // выводим db
  void createTable();
  void createCheckTable();  // таблица с маской требуемых дней
  void updToDefaultTable();  // создаем таблицу по умолчанию(пример)
  void createEmptyTable();  // создаем пустую таблицу
  void writeTable();  // записать из текущей таблицы в db
};

#endif // MAINWINDOW_H

//----------------------------------------------------------------------------