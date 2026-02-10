#pragma once

#include <QString>
#include <QTableWidget>

// для управления основной таблицей:
class MainTableManager {
public:
  // Создает менеджер основной таблицы.
  MainTableManager();
  // Освобождает ресурсы менеджера.
  ~MainTableManager();
  //   MainTableManager(QTableWidget* tableWidget);
  // Привязывает внешний QTableWidget к менеджеру.
  void setTableWidget(QTableWidget *tableWidget);
  // Заполняет таблицу тестовыми данными по умолчанию.
  void updToDefaultTable();
  // Сохраняет таблицу в БД (устаревший путь).
  void writeTable();
  // Создает пустую таблицу под месяц.
  void createEmptyTable();
  // Обновляет содержимое таблицы.
  void updateTable();
  // Добавляет пользователя в таблицу.
  void addUser(const QString &name);
  // Удаляет пользователя из таблицы.
  void delUser(const QString &name);
  // Ищет строку пользователя по имени.
  int searchName(const QString &searched_name) const;
  // Ищет колонку даты.
  int searchDate(const QString &date_in_db) const;
  // Добавляет чекбокс в ячейку.
  void addCheckBox(int row, int column, bool is_checked);

private:
  QTableWidget *tableWidget; // = nullptr;
  int month;
  int year;
  int day_in_month;
};
