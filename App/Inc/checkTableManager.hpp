#pragma once

#include <QTableWidget>

// Для управления таблицей шаблона чекбоксов.
class CheckTableManager {
 public:
  // Создает менеджер таблицы чекбоксов.
  CheckTableManager();
  // Освобождает ресурсы менеджера.
  ~CheckTableManager();
  // Создает таблицу чекбоксов (заготовка API).
  void createCheckTable();
  // Привязывает внешний виджет таблицы.
  void setCheckTable(QTableWidget* tableWidget);

 private:
  QTableWidget* tableWidget;
};
