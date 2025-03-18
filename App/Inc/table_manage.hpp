#ifndef TABLEMANAGE_H
#define TABLEMANAGE_H

#include <QObject>
#include <QSqlDatabase>
#include <QTableWidget>

class TableManager : public QObject
{
    Q_OBJECT

public:
    explicit TableManager(QObject *parent = nullptr);
    ~TableManager();

    // Методы для работы с таблицей

    void updateTable(QTableWidget *tableWidget);  // Обновление данных в таблице
    void loadData(QTableWidget *tableWidget);     // Загрузка данных из базы
    void writeTable(QTableWidget *tableWidget);   // Запись данных в базу

    // Методы для управления пользователями
    void addUser(const QString &name);
    void delUserById(int id);
    void delUserByName(const QString &name);

private:
    QSqlDatabase db;  // База данных
    // QWidget* cntrlWidget;   // это основной виджет, оттуда всё рисуется

    int month;
    int year;


    // Вспомогательные методы
    bool createConnection();          // Создание соединения с базой
    void createCheckTable();          // Создание таблицы с маской дней
    void createEmptyTable(QTableWidget *tableWidget);          // Создание пустой таблицы
};

#endif // TABLEMANAGE_H