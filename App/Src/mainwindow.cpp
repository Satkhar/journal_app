#include "mainwindow.hpp"
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <iostream>
#include <QPushButton>
#include <QSqlTableModel>

#include "config.h"

//----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  // тут можно какие-нибудь базовае общие вещи ставить в qt designer
  ui->setupUi(this); // Инициализация нарисованного интерфейса

  // тут осмысленное наполнение интерйеса
  // Создаем таблицу
  QTableWidget *tableWidget = new QTableWidget(5, 3, this);      // 5 строк, 3 столбца
  tableWidget->setHorizontalHeaderLabels({"ID", "Name", "Age"}); // Заголовки столбцов

  // Добавляем данные в таблицу
  tableWidget->setItem(0, 0, new QTableWidgetItem("1"));
  tableWidget->setItem(0, 1, new QTableWidgetItem("Alice"));
  tableWidget->setItem(0, 2, new QTableWidgetItem("25"));

  tableWidget->setItem(1, 0, new QTableWidgetItem("2"));
  tableWidget->setItem(1, 1, new QTableWidgetItem("Bob"));
  tableWidget->setItem(1, 2, new QTableWidgetItem("30"));

  // находим контейнер в UI-файле и добавляем туда таблицу
  QGridLayout *layout = ui->centralwidget->findChild<QGridLayout *>("gridLayout");
  if (layout)
  {
    layout->addWidget(tableWidget);
  }
  else
  {
    qDebug() << "Контейнер gridLayout не найден!";
  }

  // читаем базу
  if (!createConnection())
  {
    QMessageBox::critical(this, "Ошибка",
                          "Не удалось подключиться к базе данных.");
    return;
  }

  connect(ui->btnAdd, &QPushButton::clicked, this, [this]()
          {
            addUser(ui->lineEdit->text(), 30);
            // addUser("Новый пользователь", 30);
            // loadTableData(); // Обновляем таблицу
          });

  connect(ui->btnDel, &QPushButton::clicked, this, [this]()
          {
            delUserByName(ui->lineEdit->text());
            
            // delUserById(1);   // Удалить пользователя с ID = 1
            // loadTableData(); // Обновляем таблицу
          });

  connect(ui->btnViewAll, &QPushButton::clicked, this, [this]()
          {
            // delUserById(1);   // Удалить пользователя с ID = 1
            viewTableData(); // Обновляем таблицу
          });
}

//----------------------------------------------------------------------------

MainWindow::~MainWindow()
{
  delete ui;

  if (db.isOpen())
  {
    db.close();
  }
}

//----------------------------------------------------------------------------

bool MainWindow::createConnection()
{
  db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName(DB_PATH);

  if (!db.open())
  {
    // qDebug() << "Ошибка подключения к базе данных:" << db.lastError().text();
    return false;
  }

  QSqlQuery query;
  if (!query.exec("CREATE TABLE IF NOT EXISTS users ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT, "
                  "age INTEGER)"))
  {
    qDebug() << "Ошибка создания таблицы:" << query.lastError().text();
    return false;
  }
  else
  {
    qDebug() << "Таблица создана.";
  }

  return true;
}

//----------------------------------------------------------------------------

void MainWindow::loadTableData()
{
  QSqlQuery query;

  // Читаем данные из таблицы
  if (!query.exec("SELECT id, name, age FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
  }
  else
  {
    while (query.next())
    {
      int id = query.value(0).toInt();
      QString name = query.value(1).toString();
      int age = query.value(2).toInt();
      qDebug() << "ID:" << id << "Name:" << name << "Age:" << age;
    }
  }
}

//----------------------------------------------------------------------------

void MainWindow::viewTableData()
{
  QSqlQuery query("SELECT id, name, age FROM users");

  // просто
  // // Читаем результаты запроса
  // while (query.next())
  // {
  //   QSqlRecord record = query.record(); // Получаем запись (строку)
  //   qDebug() << "\nRecord count is:" << record.count();

  //   // Перебираем все поля в записи
  //   for (int i = 0; i < record.count(); ++i)
  //   {
  //     QSqlField field = record.field(i); // Получаем поле

  //     qDebug() << "Field name:" << field.name();
  //     qDebug() << "Data type:" << QMetaType(field.metaType().id()); // field.type();
  //     qDebug() << "Value:" << field.value();
  //     // qDebug() << "is NULL:" << field.isNull();
  //     // qDebug() << "Length:" << field.length();
  //     // qDebug() << "Read only:" << field.isReadOnly();
  //     qDebug() << "-------------------------";
  //   }
  //   int id = query.value(0).toInt();          // Получаем значение первого столбца
  //   QString name = query.value(1).toString(); // Получаем значение второго столбца
  //   qDebug() << "ID:" << id << "Name:" << name;
  // }

  // перез table

  // Использование QSqlTableModel
  QSqlTableModel model;
  model.setTable("users");
  model.select();

  for (int i = 0; i < model.rowCount(); ++i)
  {
    int id = model.data(model.index(i, 0)).toInt();
    QString name = model.data(model.index(i, 1)).toString();
    int age = model.data(model.index(i, 2)).toInt();
    qDebug() << "ID:" << id << "Name:" << name << "Age:" << age;
  }
}

//----------------------------------------------------------------------------

void MainWindow::addUser(const QString &name, int age)
{
  QSqlQuery query;
  query.prepare("INSERT INTO users (name, age) VALUES (:name, :age)");
  query.bindValue(":name", name);
  query.bindValue(":age", age);

  if (!query.exec())
  {
    // qDebug() << "Ошибка добавления пользователя:" << query.lastError().text();
    // std::cout << "Ошибка добавления пользователя:\n";

    return;
  }

  // qDebug() << "Пользователь добавлен!";
  // std::cout << "Пользователь добавлен!\n";
}

//----------------------------------------------------------------------------

void MainWindow::delUserById(int id_to_del)
{
  QSqlQuery query;

  query.prepare("DELETE FROM users WHERE id = :id"); // Условие на равенство
  query.bindValue(":id", id_to_del); // Привязываем значение

  if (!query.exec())
  {
    qDebug() << "Delete error:" << query.lastError().text();
  }
  else
  {
    qDebug() << id_to_del <<" Id is deleted.";
  }
}

//----------------------------------------------------------------------------

void MainWindow::delUserByName(const QString &name_to_del)
{
  QSqlQuery query;

  query.prepare("DELETE FROM users WHERE name = :name"); // Условие на равенство
  query.bindValue(":name", name_to_del); // Привязываем значение

  if (!query.exec())
  {
    qDebug() << "Delete error:" << query.lastError().text();
  }
  else
  {
    qDebug() << name_to_del <<" Name is deleted.";
  }
}

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

void MainWindow::updateTable()
{
  // Устанавливаем количество строк и столбцов
  // int rowCount = 5;    // Количество строк
  // int columnCount = 3; // Количество столбцов

  // baseTableWidget->setRowCount(rowCount);
  // baseTableWidget->setColumnCount(columnCount);

  // // Устанавливаем заголовки столбцов
  // QStringList headers;
  // headers << "Имя" << "Возраст" << "Город";
  // baseTableWidget->setHorizontalHeaderLabels(headers);

  // // Заполняем таблицу данными
  // QStringList names = {"Алексей", "Мария", "Иван", "Елена", "Сергей"};
  // QStringList ages = {"25", "30", "22", "28", "35"};
  // QStringList cities = {"Москва", "Санкт-Петербург", "Новосибирск",
  //                       "Екатеринбург", "Казань"};

  // for (int row = 0; row < rowCount; ++row)
  // {
  //   QTableWidgetItem *nameItem = new QTableWidgetItem(names[row]);
  //   QTableWidgetItem *ageItem = new QTableWidgetItem(ages[row]);
  //   QTableWidgetItem *cityItem = new QTableWidgetItem(cities[row]);

  //   baseTableWidget->setItem(row, 0, nameItem);
  //   baseTableWidget->setItem(row, 1, ageItem);
  //   baseTableWidget->setItem(row, 2, cityItem);
  // }
  // baseTableWidget->setGeometry(
  //     QRect(50, 100, (50 + rowCount * 50), (50 + columnCount * 50)));

  // // Настройка внешнего вида таблицы
  // // Автоматическая настройка ширины столбцов
  // baseTableWidget->resizeColumnsToContents();
  // // Растягивание последнего столбца
  // baseTableWidget->horizontalHeader()->setStretchLastSection(true);

  // // Добавляем таблицу в центральный виджет окна
  // // setCentralWidget(tableWidget);
}

//----------------------------------------------------------------------------

void UpdateWindow(QMainWindow *MainWindow)
{
}

//----------------------------------------------------------------------------
