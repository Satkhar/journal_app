#include "table_manage.hpp"
#include "config.h"

#include <QSqlQuery>
#include <QDebug>
#include <QCalendarWidget>
#include <QHeaderView>

TableManager::TableManager(QObject *parent)
    : QObject(parent)
{
    if (!createConnection()) {
        qWarning() << "Failed to connect to database!";
    }

    // QWidget cntrlWidget = parent->findChild<QWidget *>("centralwidget");
    
    QCalendarWidget *clndWidget = parent->findChild<QCalendarWidget *>("calendarWidget");
    month =  clndWidget->monthShown();
    year = clndWidget->yearShown();
}

TableManager::~TableManager()
{
    if (db.isOpen()) {
        db.close();
    }
}


//----------------------------------------------------------------------------

bool TableManager::createConnection()
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
    // QTableWidget *tableWidget = parent ui->centralwidget->findChild<QTableWidget *>(
    //     "bigTable");
    createEmptyTable();  
    updToDefaultTable();
    writeTable();
  }
  else
  {
    qDebug() << "not empty:";
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

void TableManager::createEmptyTable(QTableWidget *tableWidget)
{
  month;
   year;
  // Первый день текущего месяца
//   int day_in_month = max_days = QDate(year, month, 1).daysInMonth();
  int day_in_month = QDate(year, month, 1).daysInMonth();

//   QTableWidget *tableWidget =
//       new QTableWidget(3, 2 + day_in_month, this);  // 1 строк, 7 столбца
tableWidget->setRowCount(3);
tableWidget->setColumnCount(2+day_in_month);
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