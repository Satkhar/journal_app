#include "mainTableManager.hpp"

MainTableManager::MainTableManager() { tableWidget = nullptr; }

void MainTableManager::setTableWidget(QTableWidget *tableWidget) {
  this->tableWidget = tableWidget;
}

MainTableManager::~MainTableManager() {
  // if (tableWidget) {
  //   delete tableWidget; // Освобождаем память, если tableWidget был создан
  //                       // динамически
  // }
}