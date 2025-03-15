#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QtSql>
#include <iostream>

#include "mainwindow.hpp"

int main(int argc, char *argv[]) {
  std::cout << "Hello, from journal_app!\n";

  QApplication app(argc, argv);

  MainWindow mainWindow;
  mainWindow.show();

  return app.exec();
}
