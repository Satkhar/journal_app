#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <iostream>

// #include "../Inc/journal_app.h"
// #include "journal_app.h"
#include "mainwindow.hpp"

int main(int argc, char *argv[]) {
  //   std::cout << "Hello, from journal_app!\n";
  //   return 0;
  QApplication app(argc, argv);

  // Ui::MainWindow mainWindow;

  MainWindow mainWindow;
    mainWindow.show();
//   mainWindow.retranslateUi(mainWindow);
//   mainWindow.setupUi(mainWindow);
//   mainWindow.setWindowTitle("My Qt Application");
//   mainWindow.resize(800, 600);
//   mainWindow.show();

  return app.exec();
}
