#include "mainwindow.hpp"
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <iostream>
#include <QPushButton>

#include "config.h"

//----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  // тут можно какие-нибудь базовае общие вещи ставить в qt designer
  ui->setupUi(this); // Инициализация нарисованного интерфейса
  // тут осмысленное наполнение интерйеса
  CreateBase(this);

  // читаем базу
  if (!createConnection()) {
    QMessageBox::critical(this, "Ошибка",
                          "Не удалось подключиться к базе данных.");
    return;
  }

  connect(ui->btnAdd, &QPushButton::clicked, this, [this]() {
    addUser("Новый пользователь", 30);
    loadTableData(); // Обновляем таблицу
  });

  connect(ui->btnDel, &QPushButton::clicked, this, [this]() {
    // deleteUser(1);   // Удалить пользователя с ID = 1
    loadTableData(); // Обновляем таблицу
  });

  // тут заполняем таблицу (ссылка на таблицу из CreateBase?)
  MakeTable();
}

//----------------------------------------------------------------------------

MainWindow::~MainWindow() {
  delete ui;

  if (db.isOpen()) {
    db.close();
  }
}

//----------------------------------------------------------------------------

bool MainWindow::createConnection() {
  db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName(DB_PATH);

  if (!db.open()) {
    // qDebug() << "Ошибка подключения к базе данных:" << db.lastError().text();
    return false;
  }

  QSqlQuery query;
  if (!query.exec("CREATE TABLE IF NOT EXISTS users ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT, "
                  "age INTEGER)")) {
    // qDebug() << "Ошибка создания таблицы:" << query.lastError().text();
    return false;
  }

  return true;
}

//----------------------------------------------------------------------------

void MainWindow::loadTableData() {
  QSqlQuery query("SELECT id, name, age FROM users");
}

//----------------------------------------------------------------------------

void MainWindow::addUser(const QString &name, int age) {
  QSqlQuery query;
  query.prepare("INSERT INTO users (name, age) VALUES (:name, :age)");
  query.bindValue(":name", name);
  query.bindValue(":age", age);

  if (!query.exec()) {
    // qDebug() << "Ошибка добавления пользователя:" << query.lastError().text();
    // std::cout << "Ошибка добавления пользователя:\n";

    return;
  }

  // qDebug() << "Пользователь добавлен!";
  // std::cout << "Пользователь добавлен!\n";
}

//----------------------------------------------------------------------------

void MainWindow::CreateBase(QMainWindow *MainWindow) {
  ui->centralwidget = new QWidget(MainWindow);
  ui->centralwidget->setObjectName("centralwidget");
  ui->calendarWidget = new QCalendarWidget(ui->centralwidget);
  ui->calendarWidget->setObjectName("calendarWidget");
  ui->calendarWidget->setGeometry(QRect(540, 0, 256, 190));

  ui->btnAdd = new QPushButton(ui->centralwidget);
  ui->btnAdd->setObjectName("btnAdd");
  ui->btnAdd->setGeometry(QRect(30, 20, 75, 24));

  ui->btnDel = new QPushButton(ui->centralwidget);
  ui->btnDel->setObjectName("btnDel");
  ui->btnDel->setGeometry(QRect(120, 20, 75, 24));

  ui->btnAdd->setText("Add User");
  ui->btnDel->setText("Del User");

  MainWindow->setCentralWidget(ui->centralwidget);

  baseTableWidget = new QTableWidget(ui->centralwidget);

  // Устанавливаем количество строк и столбцов
  int rowCount = 1;    // Количество строк
  int columnCount = 2; // Количество столбцов
  baseTableWidget->setRowCount(rowCount);
  baseTableWidget->setColumnCount(columnCount);
}

//----------------------------------------------------------------------------

void MainWindow::MakeTable() {
  // Устанавливаем количество строк и столбцов
  int rowCount = 5;    // Количество строк
  int columnCount = 3; // Количество столбцов

  baseTableWidget->setRowCount(rowCount);
  baseTableWidget->setColumnCount(columnCount);

  // Устанавливаем заголовки столбцов
  QStringList headers;
  headers << "Имя" << "Возраст" << "Город";
  baseTableWidget->setHorizontalHeaderLabels(headers);

  // Заполняем таблицу данными
  QStringList names = {"Алексей", "Мария", "Иван", "Елена", "Сергей"};
  QStringList ages = {"25", "30", "22", "28", "35"};
  QStringList cities = {"Москва", "Санкт-Петербург", "Новосибирск",
                        "Екатеринбург", "Казань"};

  for (int row = 0; row < rowCount; ++row) {
    QTableWidgetItem *nameItem = new QTableWidgetItem(names[row]);
    QTableWidgetItem *ageItem = new QTableWidgetItem(ages[row]);
    QTableWidgetItem *cityItem = new QTableWidgetItem(cities[row]);

    baseTableWidget->setItem(row, 0, nameItem);
    baseTableWidget->setItem(row, 1, ageItem);
    baseTableWidget->setItem(row, 2, cityItem);
  }
  baseTableWidget->setGeometry(
      QRect(50, 100, (50 + rowCount * 50), (50 + columnCount * 50)));

  // Настройка внешнего вида таблицы
  // Автоматическая настройка ширины столбцов
  baseTableWidget->resizeColumnsToContents();
  // Растягивание последнего столбца
  baseTableWidget->horizontalHeader()->setStretchLastSection(true);

  // Добавляем таблицу в центральный виджет окна
  // setCentralWidget(tableWidget);
}

//----------------------------------------------------------------------------

void UpdateWindow(QMainWindow *MainWindow)
{
  
}

//----------------------------------------------------------------------------
