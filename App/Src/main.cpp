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
  // locale влияет на вывод текста и форматирование в стандартной библиотеке.
  std::setlocale(LC_ALL, ".UTF-8");

  // Небольшой trace в консоль, чтобы видеть успешный старт процесса.
  std::cout << "Hello, from journal_app!\n";
  qInfo() << "journal_app started";

  // QApplication поднимает event loop Qt и регистрирует системные ресурсы GUI.
  QApplication app(argc, argv);

  // Главное окно само внутри инициализирует UI и подключение к storage.
  MainWindow mainWindow;
  mainWindow.show();

  // Здесь поток управления передается циклу обработки событий Qt.
  return app.exec();
}

//---------------------------------------------------------------
