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

  return true;
}

bool DatabaseManager::isDatabaseEmpty()
{
  QSqlQuery query;
  // берем данные
  if (!query.exec("SELECT * FROM users"))
  {
    qDebug() << "Ошибка выполнения запроса:" << query.lastError().text();
    return false;
  }
  return !query.next();// Если нет записей, база пуста
}

DatabaseManager::~DatabaseManager()
{
  
}