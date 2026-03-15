#include "checkTableManager.hpp"

CheckTableManager::CheckTableManager()
{
  // Legacy-заготовка: полноценная логика сейчас живет в MainWindow::createCheckTable().
}

//---------------------------------------------------------------

void CheckTableManager::setCheckTable(QTableWidget* tableWidget)
{
  // Менеджер не владеет таблицей, только хранит внешний указатель.
  this->tableWidget = tableWidget;
}

//---------------------------------------------------------------

// деструктор
CheckTableManager::~CheckTableManager()
{
  // Память QTableWidget освобождает владелец виджета, не этот helper.
}

//---------------------------------------------------------------
