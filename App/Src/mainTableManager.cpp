#include "mainTableManager.hpp"

MainTableManager::MainTableManager() { tableWidget = nullptr; }

void MainTableManager::setTableWidget(QTableWidget *tableWidget) {
  this->tableWidget = tableWidget;
}

void MainTableManager::updToDefaultTable()
{
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
  tableWidget->setItem(2, 0, new QTableWidgetItem("example"));
  tableWidget->setItem(2, 1, new QTableWidgetItem("Alice"));

  // заполнение таблицы чекбоксами
  for (int day = 1; day <= day_in_month; ++day)
  {
    addCheckBox(tableWidget, 2, 1 + day,
                static_cast<bool>(day % 2));  // добавление чекбокса
  }

  // Автоматически подстраиваем ширину под содержимое
  tableWidget->resizeColumnsToContents();
}

void MainTableManager::writeTable()
{
  QElapsedTimer timer_write_table;
  timer_write_table.start();

  QSqlQuery query;

  QDate currentDate = QDate::currentDate();  // Текущая дата
  QString dateString =
      currentDate.toString("yyyy-MM-dd");  // Преобразуем в строку

  // qDebug() << "init env:" << timer_write_table.elapsed() << "ms";

  int temp = 0;
  temp = tableWidget->rowCount();
  // проходим по строкам (пользователям)
  db.transaction();

  for (int row = 2; row < tableWidget->rowCount(); ++row)
  {
    // qDebug() << "row:" << row << "time: " << timer_write_table.elapsed()
            //  << "ms";

    // проходим по всем датам(дням)
    for (int column = 2; column < tableWidget->columnCount(); ++column)
    {
      // qDebug() << "column:" << column << "time: " << timer_write_table.elapsed()
              //  << "ms";

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
        db.rollback();
      }
      else
      {
        // qDebug() << "Insert: name =" << name << ", date =" << date
        //          << ", is_checked =" << isChecked;
      }
    }
  }
  db.commit();

}


MainTableManager::~MainTableManager() {
  // if (tableWidget) {
  //   delete tableWidget; // Освобождаем память, если tableWidget был создан
  //                       // динамически
  // }
}