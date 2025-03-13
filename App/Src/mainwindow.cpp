#include "mainwindow.hpp"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this); // Инициализация интерфейса

  // Пример: Подключение сигналов и слотов
  connect(ui->pushButton, &QPushButton::clicked, this,
          [this]() { ui->statusbar->showMessage("OpenBase button clicked!"); });

  connect(ui->pushButton_2, &QPushButton::clicked, this,
          [this]() { ui->statusbar->showMessage("SaveBase button clicked!"); });
}

MainWindow::~MainWindow() {
  delete ui; // Освобождение памяти
}