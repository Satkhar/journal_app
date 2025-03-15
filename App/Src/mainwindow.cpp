#include "mainwindow.hpp"
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <iostream>
#include <QPushButton>
#include <QSqlTableModel>
#include <QCheckBox>

#include "config.h"

//----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  // тут можно какие-нибудь базовае общие вещи ставить в qt designer
  ui->setupUi(this); // Инициализация нарисованного интерфейса

  // создать таблицу выбора учитываемых дней
  createCheckTable();

  createDefaultTable();

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
            loadTableData();
            // delUserById(1);   // Удалить пользователя с ID = 1
            viewTableData(); // Обновляем таблицу
          });

  connect(ui->btnCreateTable, &QPushButton::clicked, this, [this]()
          {
            createTable();
            // loadTableData();
            // delUserById(1);   // Удалить пользователя с ID = 1
            // viewTableData(); // Обновляем таблицу
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

  updateTable();
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

void MainWindow::createTable()
{
  // QDate &date =
  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();
  // Первый день текущего месяца
  QDate firstDayOfMonth = QDate(year, month, 1);
  int dayOfWeek = firstDayOfMonth.dayOfWeek(); // 1 - понедельник, ..., 7 - воскресенье

  int day = ui->calendarWidget->firstDayOfWeek();

  int day_in_month = QDate(year, month, 1).daysInMonth();

  qDebug() << "month:" << month << "Day:" << day;
  qDebug() << "dayOfWeek:" << dayOfWeek << "day_in_month" << day_in_month;

  // calendarWidget
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
  query.bindValue(":id", id_to_del);                 // Привязываем значение

  if (!query.exec())
  {
    qDebug() << "Delete error:" << query.lastError().text();
  }
  else
  {
    qDebug() << id_to_del << " Id is deleted.";
  }
}

//----------------------------------------------------------------------------

void MainWindow::delUserByName(const QString &name_to_del)
{
  QSqlQuery query;

  query.prepare("DELETE FROM users WHERE name = :name"); // Условие на равенство
  query.bindValue(":name", name_to_del);                 // Привязываем значение

  if (!query.exec())
  {
    qDebug() << "Delete error:" << query.lastError().text();
  }
  else
  {
    qDebug() << name_to_del << " Name is deleted.";
  }
}

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

void MainWindow::updateTable()
{
  // ищём нужный нам виджет (таблицу)
  QTableWidget *tableWidget = ui->centralwidget->findChild<QTableWidget *>("bigTable"); // new QTableWidget(5, 3, this);      // 5 строк, 3 столбца
  // Очищаем таблицу перед загрузкой новых данных
  tableWidget->clearContents();
  tableWidget->setRowCount(0);

  // Выполняем SQL-запрос
  QSqlQuery query;
  if (!query.exec("SELECT id, name, age FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
    return;
  }

  // Добавляем данные в таблицу
  int row = 0;
  while (query.next())
  {
    // через индексы, наверное, удобнее будет
    int id = query.value(0).toInt();
    QString name = query.value(1).toString();
    int age = query.value(2).toInt();

    // Добавляем новую строку в таблицу
    tableWidget->insertRow(tableWidget->rowCount());

    // Заполняем ячейки данными
    tableWidget->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
    tableWidget->setItem(row, 1, new QTableWidgetItem(name));
    tableWidget->setItem(row, 2, new QTableWidgetItem(QString::number(age)));

    row++;
  }
}

//----------------------------------------------------------------------------

void MainWindow::createCheckTable()
{
  // Создаем таблицу
  QTableWidget *tableWidget = new QTableWidget(1, 7, this); // 2 строки, 7 столбцов
  // заголовки столбцов
  tableWidget->setHorizontalHeaderLabels({
      "Пн",
      "Вт",
      "Ср",
      "Чт",
      "Пт",
      "Сб",
      "Вск",
  });
  tableWidget->setVerticalHeaderLabels({" "});

  tableWidget->setObjectName("checkTable");

  // Добавляем чекбоксы в каждую ячейку
  for (int row = 0; row < tableWidget->rowCount(); ++row)
  {
    for (int col = 0; col < tableWidget->columnCount(); ++col)
    {
      QCheckBox *checkBox = new QCheckBox();          // Создаем чекбокс
      tableWidget->setCellWidget(row, col, checkBox); // Размещаем чекбокс в ячейке
      if (col < 5)
        checkBox->setChecked(true);
    }
  }
  // Минимальная ширина — 50 пикселей
  // tableWidget->horizontalHeader()->setMinimumSectionSize(50);

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();

  // Разрешаем интерактивное изменение размера столбцов
  tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

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
}

//----------------------------------------------------------------------------

void MainWindow::createDefaultTable()
{
  // тут осмысленное наполнение интерйеса
  // Создаем таблицу
  QTableWidget *tableWidget = new QTableWidget(3, 31, this); // 1 строк, 7 столбца
  tableWidget->setHorizontalHeaderLabels({
      "ID",
      "Name",
      "Age",
  }); // Заголовки столбцов
  tableWidget->setObjectName("bigTable");
  // Добавляем данные в таблицу
  tableWidget->setItem(0, 0, new QTableWidgetItem(" "));
  tableWidget->setItem(0, 1, new QTableWidgetItem(" "));
  tableWidget->setItem(0, 2, new QTableWidgetItem(" "));

  int month = ui->calendarWidget->monthShown();

  for (int day = 1; day <= 31; ++day)
  {
    QString text = QString("%1.%2").arg(day, 2, 10, QLatin1Char('0')) // Добавляет нули перед числом
                       .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, 2 + day, new QTableWidgetItem(text));
  }
  tableWidget->setItem(1, 0, new QTableWidgetItem(" "));
  tableWidget->setItem(1, 1, new QTableWidgetItem(" "));
  tableWidget->setItem(1, 2, new QTableWidgetItem(" "));

  // BUG эт завтра уже. тут надо день недели ставить
  for (int day = 1; day <= 31; ++day)
  {
    QString text = QString("%1.%2").arg(day, 2, 10, QLatin1Char('0')) // Добавляет нули перед числом
                       .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, 2 + day, new QTableWidgetItem(text));
  }


  tableWidget->setItem(2, 0, new QTableWidgetItem("1"));
  tableWidget->setItem(2, 1, new QTableWidgetItem("Alice"));
  tableWidget->setItem(2, 2, new QTableWidgetItem("25"));

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();

  // Разрешаем интерактивное изменение размера столбцов
  tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

  // tableWidget->setItem(0, 2, new QTableWidgetItem("25"));

  // tableWidget->setItem(1, 0, new QTableWidgetItem("2"));
  // tableWidget->setItem(1, 1, new QTableWidgetItem("Bob"));
  // tableWidget->setItem(1, 2, new QTableWidgetItem("30"));

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
}

//----------------------------------------------------------------------------
