#pragma once

#include <QTableWidget>



// для управления таблицей чекбоксов
  class CheckTableManager {
    public:
        CheckTableManager();
        ~CheckTableManager();
        void createCheckTable();
        void setCheckTable(QTableWidget* tableWidget);
        
    private:
        QTableWidget* tableWidget;
    };