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

  // пустую таблицу делаем
  createEmptyTable();
  // создать таблицу выбора учитываемых дней
  createCheckTable();

  // читаем базу
  if (!createConnection())
  {
    QMessageBox::critical(this, "Ошибка",
                          "Не удалось подключиться к базе данных.");
    return;
  }

  connect(ui->btnAdd, &QPushButton::clicked, this, [this]()
          {
            addUser(ui->lineEdit->text());
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
            // viewTableData(); // Обновляем таблицу
          });

  connect(ui->btnCreateTable, &QPushButton::clicked, this, [this]()
          {
            createTable();
            // loadTableData();
            // delUserById(1);   // Удалить пользователя с ID = 1
            // viewTableData(); // Обновляем таблицу
          });

  // это надо если нет таблицы из db
  updToDefaultTable();
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
    qDebug() << "Ошибка подключения к базе данных:" << db.lastError().text();
    return false;
  }

  QSqlQuery query;

  // создаем если пусто
  // делаем нормализованную, а не широкую структуру
  if (!query.exec("CREATE TABLE IF NOT EXISTS users ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL, "
                  "date DATE NOT NULL, "
                  "is_checked BOOLEAN NOT NULL )"))
  {
    qDebug() << "Ошибка создания таблицы:" << query.lastError().text();
    return false;
  }
  // берем данные
  if (!query.exec("SELECT * FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
    return false;
  }

  // Проверяем, есть ли данные
  if (!query.next())
  {
    qDebug() << "empty";
    updToDefaultTable();
    writeTable();
  }
  else
  {
    qDebug() << "not empty:";
    do
    {
      qDebug() << "ID:" << query.value(0).toInt()
               << "Name:" << query.value(1).toString()
               << "Date:" << query.value(2).toString()
               << "Checked:" << query.value(3).toBool();
    } while (query.next());
  }

  return true;
}

//----------------------------------------------------------------------------

void MainWindow::loadTableData()
{
  QSqlQuery query;

  // Читаем данные из таблицы
  if (!query.exec("SELECT id, name, date FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
  }
  else
  {
    while (query.next())
    {
      int id = query.value(0).toInt();
      QString name = query.value(1).toString();
      QString date = query.value(2).toString();
      bool checked = query.value(3).toBool();
      qDebug() << "load ID:" << id << "Name:" << name << "Date: " << date << "Checked: " << checked;
    }
  }

  updateTable();
}

//----------------------------------------------------------------------------

void MainWindow::viewTableData()
{
  QSqlQuery query("SELECT id, name FROM users");

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
    qDebug() << "ID:" << id << "Name:" << name;
  }
}

//----------------------------------------------------------------------------

void MainWindow::createTable()
{
  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();
  // Первый день текущего месяца
  QDate firstDayOfMonth = QDate(year, month, 1);
  int dayOfWeek = firstDayOfMonth.dayOfWeek(); // 1 - понедельник, ..., 7 - воскресенье

  int day = ui->calendarWidget->firstDayOfWeek();

  int day_in_month = QDate(year, month, 1).daysInMonth();

  qDebug() << "month:" << month << "Day:" << day;
  qDebug() << "dayOfWeek:" << dayOfWeek << "day_in_month" << day_in_month;
}
//----------------------------------------------------------------------------

void MainWindow::addUser(const QString &name)
{
  QSqlQuery query;
  query.prepare("INSERT INTO users (name) VALUES (:name)");
  query.bindValue(":name", name);

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
  // tableWidget->setRowCount(0);

  // Добавляем данные в таблицу
  tableWidget->setItem(0, 0, new QTableWidgetItem("Дата"));
  tableWidget->setItem(1, 0, new QTableWidgetItem("День"));

  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();

  for (int day = 1; day <= 31; ++day)
  {
    QString text = QString("%1.%2").arg(day, 2, 10, QLatin1Char('0')) // Добавляет нули перед числом
                       .arg(month, 2, 10, QLatin1Char('0'));

    QTableWidgetItem *item = tableWidget->item(0, 1 + day);
    if (item)
    {
      // Если ячейка существует, обновляем её текст
      item->setText(text);
    }
    else
    {
      // Если ячейка не существует, создаем новую
      tableWidget->setItem(0, 1 + day, new QTableWidgetItem(text));
    }
  }

  for (int day = 1; day <= 31; ++day)
  {
    QDate firstDayOfMonth = QDate(year, month, day);
    int dayOfWeek = firstDayOfMonth.dayOfWeek();
    QString text = kDaysOfWeek[dayOfWeek - 1];
    tableWidget->setItem(1, 1 + day, new QTableWidgetItem(text));
  }

  // Выполняем SQL-запрос
  QSqlQuery query;
  QSqlTableModel model;
  int recordCount = 0;

  if (!query.exec("SELECT COUNT(*) FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
    return;
  }

  model.setTable("users");
  model.select();

  recordCount = model.rowCount();
  qDebug() << "recordCount:" << recordCount;

  while (recordCount > tableWidget->rowCount())
  {
    tableWidget->insertRow(tableWidget->rowCount());
  }
  int row = 2;
  while (row < recordCount)
  {
    int id = model.data(model.index(row, 0)).toInt();
    QString name = model.data(model.index(row, 1)).toString();

    tableWidget->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
    tableWidget->setItem(row, 1, new QTableWidgetItem(name));
    row++;
  }
}

//----------------------------------------------------------------------------

void MainWindow::createCheckTable()
{
  // Создаем таблицу
  QTableWidget *tableWidget = new QTableWidget(1, 7, this); // 2 строки, 7 столбцов
  // заголовки столбцов
  tableWidget->setHorizontalHeaderLabels(kDaysOfWeek);
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
  QVBoxLayout *layout = ui->centralwidget->findChild<QVBoxLayout *>("verticalLayout_3");
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

void MainWindow::updToDefaultTable()
{
  // cоздаем таблицe по умолчанию
  // заголовок id/имя
  QTableWidget *tableWidget = findChild<QTableWidget *>("bigTable");
  // tableWidget->setItem(0, 0, new QTableWidgetItem("ID"));
  // tableWidget->setItem(0, 1, new QTableWidgetItem("Name"));

  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();
  QString data;

  // запись даты (дд.мм) в таблицу
  for (int day = 1; day <= 31; ++day)
  {
    data = QString("%1.%2").arg(day, 2, 10, QLatin1Char('0')) // Добавляет нули перед числом
               .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, 1 + day, new QTableWidgetItem(data));
  }
  // пустые элементы (просто, чтобы не забыть, что они есть)
  // tableWidget->setItem(1, 0, new QTableWidgetItem(" "));
  // tableWidget->setItem(1, 1, new QTableWidgetItem(" "));
  // запись дня недели в таблицу (пн, вт и т.д.)
  for (int day = 1; day <= 31; ++day)
  {
    QDate firstDayOfMonth = QDate(year, month, day);
    int dayOfWeek = firstDayOfMonth.dayOfWeek();
    QString text = kDaysOfWeek[dayOfWeek - 1];
    tableWidget->setItem(1, 1 + day, new QTableWidgetItem(text));
  }
  // тестовый user
  tableWidget->setItem(2, 0, new QTableWidgetItem("1"));
  tableWidget->setItem(2, 1, new QTableWidgetItem("Alice"));
  tableWidget->setItem(2, 2, new QTableWidgetItem(data));
  // заполнение таблицы чекбоксами
  for (int day = 1; day <= 31; ++day)
  {
    QCheckBox *checkBox = new QCheckBox();            // Создаем чекбокс
    tableWidget->setCellWidget(2, 1 + day, checkBox); // Размещаем чекбокс в ячейке
  }

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
}

//----------------------------------------------------------------------------

void MainWindow::createEmptyTable()
{
  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();
  // Первый день текущего месяца
  int day_in_month = QDate(year, month, 1).daysInMonth();

  QTableWidget *tableWidget = new QTableWidget(3, 2 + day_in_month, this); // 1 строк, 7 столбца
  tableWidget->setObjectName("bigTable");

  // Устанавливаем заголовки для каждого столбца
  tableWidget->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
  tableWidget->setHorizontalHeaderItem(1, new QTableWidgetItem("Name"));
  for (int i = 0; i < day_in_month; ++i)
  {
    tableWidget->setHorizontalHeaderItem(2 + i, new QTableWidgetItem(" "));
  }
  tableWidget->setItem(0, 0, new QTableWidgetItem("Дата"));
  tableWidget->setItem(1, 0, new QTableWidgetItem("День"));

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

void MainWindow::writeTable()
{
  QTableWidget *tableWidget = findChild<QTableWidget *>("bigTable");
  QSqlQuery query;

  int month = ui->calendarWidget->monthShown();
  int year = ui->calendarWidget->yearShown();
  int day = ui->calendarWidget->firstDayOfWeek();

  QDate currentDate = QDate::currentDate();                // Текущая дата
  QString dateString = currentDate.toString("yyyy-MM-dd"); // Преобразуем в строку

  int temp = 0;
  temp = tableWidget->rowCount();

  for (int row = 2; row < tableWidget->rowCount(); ++row)
  {
    // Извлекаем данные из ячеек
    QString name = tableWidget->item(row, 1)->text(); // Второй столбец (string)
    QString date = QDate(year, month, 1).toString("dd.MM.yy");
    QCheckBox *checkBox = qobject_cast<QCheckBox *>(tableWidget->cellWidget(row, 2)); // Третий столбец (checkbox)
    bool isChecked = checkBox ? checkBox->isChecked() : false;

    // Формируем SQL-запрос
    query.prepare("INSERT INTO users (name, date, is_checked) VALUES (:name, :date, :is_checked)");
    query.bindValue(":name", name);
    query.bindValue(":date", date);
    query.bindValue(":is_checked", isChecked);

    // Выполняем запрос
    if (!query.exec())
    {
      qDebug() << "Error write data:" << query.lastError().text();
    }
    else
    {
      qDebug() << "Insert: name =" << name
               << ", date =" << date
               << ", is_checked =" << isChecked;
    }
  }
}