#include <QTableWidget>



// для управления таблицей чекбоксов
  class CheckTableManager {
    public:
        CheckTableManager(QTableWidget* tableWidget);
        void createCheckTable();
        
    private:
        QTableWidget* tableWidget;
    };