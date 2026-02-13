#include "checkTableManager.hpp"

CheckTableManager::CheckTableManager()
{
  // прописать нормально как наследника от маинвиндов
}

//---------------------------------------------------------------

void CheckTableManager::setCheckTable(QTableWidget* tableWidget)
{
  this->tableWidget = tableWidget;
}

//---------------------------------------------------------------

// деструктор
CheckTableManager::~CheckTableManager()
{

}

//---------------------------------------------------------------
