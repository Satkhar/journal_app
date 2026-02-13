#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QDebug>
#include <QtSql>
#include <clocale>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "mainwindow.hpp"

//---------------------------------------------------------------

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
  // Для корректного вывода кириллицы в консоль Windows.
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  std::setlocale(LC_ALL, ".UTF-8");

  std::cout << "Hello, from journal_app!\n";
  qInfo() << "journal_app started";

  QApplication app(argc, argv);

  MainWindow mainWindow;
  mainWindow.show();

  return app.exec();
}

//---------------------------------------------------------------
