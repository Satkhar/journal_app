#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "journal_app.h" // Подключаем сгенерированный файл

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui; // Указатель на объект UI
};

#endif // MAINWINDOW_H