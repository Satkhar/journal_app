#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlTableModel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <iostream>

#include "config.h"

//----------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  // тут можно какие-нибудь базовае общие вещи ставить в qt designer
  ui->setupUi(this);  // Инициализация нарисованного интерфейса

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

  connect(ui->btnAdd, &QPushButton::clicked, this,
          [this]()
          {
            addUser(ui->lineEdit->text(),
                    findChild<QTableWidget *>("bigTable"));
            // addUser("Новый пользователь", 30);
            // loadTableData(); // Обновляем таблицу
          });

  connect(ui->btnDel, &QPushButton::clicked, this,
          [this]()
          {
            delUser(ui->lineEdit->text(),
                    findChild<QTableWidget *>("bigTable"));

            // delUser(1);   // Удалить пользователя с ID = 1
            // loadTableData(); // Обновляем таблицу
          });

  connect(ui->btnReadBase, &QPushButton::clicked, this,
          [this]()
          {
            loadTableData();
            // delUser(1);   // Удалить пользователя с ID = 1
            // viewTableData(); // Обновляем таблицу
          });

  connect(ui->btnCreateTable, &QPushButton::clicked, this,
          [this]()
          {
            createTable();
            // loadTableData();
            // delUser(1);   // Удалить пользователя с ID = 1
            // viewTableData(); // Обновляем таблицу
          });

  connect(ui->btnSaveCurTable, &QPushButton::clicked, this,
          [this]()
          {
            clearDB(db, "users");
            writeTable();
            // createTable();
            // loadTableData();
            // delUser(1);   // Удалить пользователя с ID = 1
            // viewTableData(); // Обновляем таблицу
          });

  // это надо если нет таблицы из db
  // updToDefaultTable();
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
                  "date TEXT NOT NULL, "  // DATE
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
    // loadTableData();
    updateTable();
    do
    {
      // qDebug() << "ID:" << query.value(0).toInt()
      //          << "Name:" << query.value(1).toString()
      //          << "Date:" << query.value(2).toString()
      //          << "Checked:" << query.value(3).toBool();
    } while (query.next());
  }

  return true;
}

//----------------------------------------------------------------------------

void MainWindow::loadTableData()
{
  QSqlQuery query;

  // Читаем данные из таблицы
  if (!query.exec("SELECT id, name, date, is_checked FROM users"))
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
      qDebug() << "load ID:" << id << "Name:" << name << "Date: " << date
               << "Checked: " << checked;
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
  //     qDebug() << "Data type:" << QMetaType(field.metaType().id()); //
  //     field.type(); qDebug() << "Value:" << field.value();
  //     // qDebug() << "is NULL:" << field.isNull();
  //     // qDebug() << "Length:" << field.length();
  //     // qDebug() << "Read only:" << field.isReadOnly();
  //     qDebug() << "-------------------------";
  //   }
  //   int id = query.value(0).toInt();          // Получаем значение первого
  //   столбца QString name = query.value(1).toString(); // Получаем значение
  //   второго столбца qDebug() << "ID:" << id << "Name:" << name;
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
  // int month = ui->calendarWidget->monthShown();
  // int year = ui->calendarWidget->yearShown();
  // Первый день текущего месяца
  // QDate firstDayOfMonth = QDate(year, month, 1);
  // int dayOfWeek =
  //     firstDayOfMonth.dayOfWeek();  // 1 - понедельник, ..., 7 - воскресенье

  // int day = ui->calendarWidget->firstDayOfWeek();

  // int day_in_month = QDate(year, month, 1).daysInMonth();

  // qDebug() << "month:" << month << "Day:" << day;
  // qDebug() << "dayOfWeek:" << dayOfWeek << "day_in_month" << day_in_month;
}
//----------------------------------------------------------------------------

/// @brief функция добавления пользотеля в табличку. с отрисовкой checkBox
/// @param name
void MainWindow::addUser(const QString &name, QTableWidget *tableWidget)
{
  uint32_t current_row = tableWidget->rowCount();
  // сначала проверяем, что такого ещё нет
  if (name == "")
  {
    qDebug() << "enter name";
    return;
  }

  if (searchName(tableWidget, name) == -1)
  {
    // empty table
  }
  else if (searchName(tableWidget, name) == 0)
  {
    // new user
  }
  else
  {
    qDebug() << "already exist";
    return;
  }

  tableWidget->insertRow(current_row);
  tableWidget->setItem(current_row, 0, new QTableWidgetItem("new"));
  tableWidget->setItem(current_row, 1, new QTableWidgetItem(name));

  for (int day = 1; day <= day_in_month; ++day)
  {
    addCheckBox(tableWidget, current_row, 1 + day, false);
  }

  // QSqlQuery query;
  // query.prepare("INSERT INTO users (name) VALUES (:name)");
  // query.bindValue(":name", name);

  // if (!query.exec())
  // {
  //   // qDebug() << "Ошибка добавления пользователя:" <<
  //   // query.lastError().text(); std::cout << "Ошибка добавления
  //   // пользователя:\n";

  //   return;
  // }

  // qDebug() << "Пользователь добавлен!";
  // std::cout << "Пользователь добавлен!\n";

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
}

//----------------------------------------------------------------------------

void MainWindow::delUser(int id_to_del, QTableWidget *tableWidget)
{
  QSqlQuery query;

  query.prepare("DELETE FROM users WHERE id = :id");  // Условие на равенство
  query.bindValue(":id", id_to_del);                  // Привязываем значение

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

void MainWindow::delUser(const QString &name, QTableWidget *tableWidget)
{
  // QSqlQuery query;

  // query.prepare(
  //     "DELETE FROM users WHERE name = :name");  // Условие на равенство
  // query.bindValue(":name", name_to_del);        // Привязываем значение

  // if (!query.exec())
  // {
  //   qDebug() << "Delete error:" << query.lastError().text();
  // }
  // else
  // {
  //   qDebug() << name_to_del << " Name is deleted.";
  // }
  if (name == "")
  {
    qDebug() << "enter name";
    return;
  }
  uint32_t row_to_del = searchName(tableWidget, name);

  if (row_to_del == -1)
  {
    // empty table
    qDebug() << "empty table";
    return;
  }
  else if (row_to_del == 0)
  {
    // no user
    qDebug() << "user don't found";
    return;
  }
  else
  {
    tableWidget->removeRow(row_to_del);
  }

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
  return;
}

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------

void MainWindow::updateTable()
{
  // ищём нужный нам виджет (таблицу)
  QTableWidget *tableWidget = ui->centralwidget->findChild<QTableWidget *>(
      "bigTable");  // new QTableWidget(5, 3, this);      // 5 строк, 3 столбца
  // Очищаем таблицу перед загрузкой новых данных
  tableWidget->clearContents();
  // tableWidget->setRowCount(0);

  // Добавляем данные в таблицу
  tableWidget->setItem(0, 0, new QTableWidgetItem("Дата"));
  tableWidget->setItem(1, 0, new QTableWidgetItem("День"));

  // int month = ui->calendarWidget->monthShown();
  // int year = ui->calendarWidget->yearShown();

  for (int day = 1; day <= day_in_month; ++day)
  {
    QString text =
        QString("%1.%2")
            .arg(day, 2, 10, QLatin1Char('0'))  // Добавляет нули перед числом
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

  for (int day = 1; day <= day_in_month; ++day)
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
  // сколько строк в db
  recordCount = model.rowCount();
  qDebug() << "recordCount:" << recordCount;

  int row = 0;
  int start_row = 2;
  // int start_column = 2;
  while (row < recordCount)
  {
    bool found = false;
    // bool isChecked_found = false;
    int id = model.data(model.index(row, 0)).toInt();
    QString new_name = model.data(model.index(row, 1)).toString();
    for (int search_row = start_row; search_row < tableWidget->rowCount();
         ++search_row)
    {
      QTableWidgetItem *item = tableWidget->item(search_row, 1);
      QString search_name = "";
      // проверка, есть ли вообще что-то
      if (item)
      {
        search_name = item->text();
      }

      if (!item)
      {
        // таблица есть, но пустая
        found = true;
        tableWidget->setItem(search_row, 0,
                             new QTableWidgetItem(QString::number(id)));
        tableWidget->setItem(search_row, 1, new QTableWidgetItem(new_name));
        search_name = new_name;  // как бы нашли - первая запись

        bool isChecked_in_found = model.data(model.index(row, 3)).toBool();
        addCheckBox(tableWidget, search_row, 2, isChecked_in_found);
        // break;
      }

      if (new_name == search_name)
      {
        found = true;
        QString date_in_db = model.data(model.index(row, 2)).toString();
        int column = searchDate(tableWidget, date_in_db);
        if (column == 0)
        {
          // не нашли такой даты в таблице - не надо отмечать
          break;
        }
        bool isChecked_in_found = model.data(model.index(row, 3)).toBool();
        addCheckBox(tableWidget, search_row, column, isChecked_in_found);
        break;
      }
    }

    if (found != true)
    {
      // tableWidget->insertRow(row);
      tableWidget->insertRow(tableWidget->rowCount());
      tableWidget->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
      tableWidget->setItem(row, 1, new QTableWidgetItem(new_name));

      bool isChecked_in_found = model.data(model.index(row, 3)).toBool();
      addCheckBox(tableWidget, row, 2, isChecked_in_found);
    }

    row++;
  }
  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
}

//----------------------------------------------------------------------------

void MainWindow::createCheckTable()
{
  // Создаем таблицу
  QTableWidget *tableWidget =
      new QTableWidget(1, 7, this);  // 2 строки, 7 столбцов
  // заголовки столбцов
  tableWidget->setHorizontalHeaderLabels(kDaysOfWeek);
  tableWidget->setVerticalHeaderLabels({" "});

  tableWidget->setObjectName("checkTable");

  // Добавляем чекбоксы в каждую ячейку
  for (int row = 0; row < tableWidget->rowCount(); ++row)
  {
    for (int col = 0; col < tableWidget->columnCount(); ++col)
    {
      QCheckBox *checkBox = new QCheckBox();  // Создаем чекбокс
      tableWidget->setCellWidget(row, col,
                                 checkBox);  // Размещаем чекбокс в ячейке
      if (col < 5)
      {
        checkBox->setChecked(true);
      }
    }
  }
  // Минимальная ширина — 50 пикселей
  // tableWidget->horizontalHeader()->setMinimumSectionSize(50);

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();

  // Разрешаем интерактивное изменение размера столбцов
  tableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);

  // находим контейнер в UI-файле и добавляем туда таблицу
  QVBoxLayout *layout =
      ui->centralwidget->findChild<QVBoxLayout *>("verticalLayout_3");
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

  // int month = ui->calendarWidget->monthShown();
  // int year = ui->calendarWidget->yearShown();
  QString data;

  // запись даты (дд.мм) в таблицу
  for (int day = 1; day <= day_in_month; ++day)
  {
    data =
        QString("%1.%2")
            .arg(day, 2, 10, QLatin1Char('0'))  // Добавляет нули перед числом
            .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, 1 + day, new QTableWidgetItem(data));
  }

  // запись дня недели в таблицу (пн, вт и т.д.)
  for (int day = 1; day <= day_in_month; ++day)
  {
    QDate firstDayOfMonth = QDate(year, month, day);
    int dayOfWeek = firstDayOfMonth.dayOfWeek();
    QString text = kDaysOfWeek[dayOfWeek - 1];
    tableWidget->setItem(1, 1 + day, new QTableWidgetItem(text));
  }
  // тестовый user
  tableWidget->setItem(2, 0, new QTableWidgetItem("1"));
  tableWidget->setItem(2, 1, new QTableWidgetItem("Alice"));

  // заполнение таблицы чекбоксами
  for (int day = 1; day <= day_in_month; ++day)
  {
    // QCheckBox *checkBox = new QCheckBox();  // Создаем чекбокс
    // checkBox->setChecked(static_cast<bool>(day % 2));
    // tableWidget->setCellWidget(2, 1 + day,
    //  checkBox);  // Размещаем чекбокс в ячейке
    addCheckBox(tableWidget, 2, 1 + day,
                static_cast<bool>(day % 2));  // добавление чекбокса
  }

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
}

//----------------------------------------------------------------------------

void MainWindow::createEmptyTable()
{
  MainWindow::updateCalendarVariables(ui->calendarWidget);

  QTableWidget *tableWidget =
      new QTableWidget(3, 2 + day_in_month, this);  // 1 строк, 7 столбца
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
  tableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);

  // находим контейнер в UI-файле и добавляем туда таблицу
  QGridLayout *layout =
      ui->centralwidget->findChild<QGridLayout *>("gridLayout");
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

  // int month = ui->calendarWidget->monthShown();
  // int year = ui->calendarWidget->yearShown();
  // int day = ui->calendarWidget->firstDayOfWeek();

  QDate currentDate = QDate::currentDate();  // Текущая дата
  QString dateString =
      currentDate.toString("yyyy-MM-dd");  // Преобразуем в строку

  int temp = 0;
  temp = tableWidget->rowCount();
  // проходим по строкам (пользователям)
  for (int row = 2; row < tableWidget->rowCount(); ++row)
  {
    // проходим по всем датам(дням)
    for (int column = 2; column < tableWidget->columnCount(); ++column)
    {
      // Извлекаем данные из ячеек
      QString name =
          tableWidget->item(row, 1)->text();  // Второй столбец (string)
      QString date = tableWidget->item(0, column)->text();
      // QString date = QDate(year, month, 1).toString("dd.MM.yy");
      QCheckBox *checkBox = qobject_cast<QCheckBox *>(
          tableWidget->cellWidget(row, column));  // Третий столбец (checkbox)
      bool isChecked = checkBox ? checkBox->isChecked() : false;

      // Формируем SQL-запрос
      query.prepare(
          "INSERT INTO users (name, date, is_checked) VALUES (:name, "
          ":date, :is_checked)");
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
        qDebug() << "Insert: name =" << name << ", date =" << date
                 << ", is_checked =" << isChecked;
      }
    }
  }
}

//----------------------------------------------------------------------------

bool MainWindow::clearDB(QSqlDatabase &db, const QString &tableName)
{
  QSqlQuery query(db);

  // Формируем SQL-запрос для удаления всех записей
  QString deleteQuery = QString("DELETE FROM %1").arg(tableName);

  if (!query.exec(deleteQuery))
  {
    qWarning() << "Failed to delete records:" << query.lastError().text();
    return false;
  }

  qDebug() << "All records from table" << tableName << "deleted successfully.";
  return true;
}

//----------------------------------------------------------------------------

/// @brief поиск столбца с нужной датой
/// @param tableWidget
/// @return номер столбца если нашли, 0 если не нашли
int MainWindow::searchDate(QTableWidget *tableWidget, QString date_in_db)
{
  int start_column = 2;  // начало
  for (int column = start_column; column < (day_in_month + start_column);
       ++column)
  {
    QTableWidgetItem *data_item = tableWidget->item(0, column);
    QString date_in_column = data_item->text();
    // QString date_in_db = model.data(model.index(row, 2)).toString();

    if (date_in_column == date_in_db)
    {
      return column;
    }
  }
  return 0;
}

//----------------------------------------------------------------------------

/// @brief поиск строки с нужным именем
/// @param tableWidget
/// @return номер строки если нашли, 0 если не нашли
int MainWindow::searchName(QTableWidget *tableWidget, QString searched_name)
{
  int start_row = 2;

  for (int search_row = start_row; search_row < tableWidget->rowCount();
       ++search_row)
  {
    QTableWidgetItem *item = tableWidget->item(search_row, 1);
    QString name_in_table = "";
    // проверка, есть ли вообще что-то
    if (item)
    {
      name_in_table = item->text();
    }
    else
    {
      return -1;
    }

    // if (!item)
    // {
    //   // таблица есть, но пустая
    //   found = true;
    //   // tableWidget->insertRow(search_row);
    //   tableWidget->setItem(search_row, 0,
    //                        new QTableWidgetItem(QString::number(id)));
    //   tableWidget->setItem(search_row, 1, new QTableWidgetItem(new_name));
    //   search_name = new_name;  // как бы нашли - первая запись
    //                            // is_ckecked добавить
    //                            // break;
    // }

    if (searched_name == name_in_table)
    {
      return search_row;
    }
  }
  return 0;
}

//----------------------------------------------------------------------------

void MainWindow::addCheckBox(QTableWidget *tableWidget, int row, int column,
                             bool is_checked)
{
  QCheckBox *checkBox = new QCheckBox();
  checkBox->setChecked(is_checked);
  tableWidget->setCellWidget(row, column, checkBox);
}

//----------------------------------------------------------------------------

void MainWindow::updateCalendarVariables(QCalendarWidget *calendarWidget)
{
  month = calendarWidget->monthShown();
  year = calendarWidget->yearShown();
  day_in_month = QDate(year, month, 1).daysInMonth();
}