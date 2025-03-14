#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QtSql>
#include <iostream>

// #include "../Inc/journal_app.h"
// #include "journal_app.h"
#include "mainwindow.hpp"

int main(int argc, char *argv[]) {
  std::cout << "Hello, from journal_app!\n";

  QApplication app(argc, argv);

  MainWindow mainWindow;
  mainWindow.addUser(":name", 1);
  mainWindow.show();
  //   mainWindow.retranslateUi(mainWindow);
  //   mainWindow.setupUi(mainWindow);
  //   mainWindow.setWindowTitle("My Qt Application");
  //   mainWindow.resize(800, 600);
  //   mainWindow.show();

  return app.exec();
}
