#include "dbManager.hpp"

DatabaseManager::DatabaseManager()
{

}


// void DatabaseManager::setDatabase(const QString &dbPath)
// {
  
//     db = QSqlDatabase::addDatabase("QSQLITE");
//     db.setDatabaseName(dbPath);
  
//     if (!db.open())
//     {
//       qDebug() << "Ошибка подключения к базе данных:" << db.lastError().text();
//       // return false;
//     }
// }

bool DatabaseManager::createConnection(const QString &dbPath)
{
  db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName(dbPath);

  if (!db.open())
  {
    qDebug() << "Ошибка подключения к базе данных:" << db.lastError().text();
    return true;
  }
  return false;
}

DatabaseManager::~DatabaseManager()
{
  
}