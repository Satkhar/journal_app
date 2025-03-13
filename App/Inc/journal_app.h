/********************************************************************************
** Form generated from reading UI file 'journal_app.ui'
**
** Created by: Qt User Interface Compiler version 6.8.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef JOURNAL_APPUNPCSU_H
#define JOURNAL_APPUNPCSU_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCalendarWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow {
public:
  QWidget *centralwidget;
  QCalendarWidget *calendarWidget;
  QPushButton *pushButton;
  QPushButton *pushButton_2;
  QTableView *tableView;
  QMenuBar *menubar;
  QStatusBar *statusbar;

  void setupUi(QMainWindow *MainWindow) {
    if (MainWindow->objectName().isEmpty())
      MainWindow->setObjectName("MainWindow");
    MainWindow->resize(800, 600);
    centralwidget = new QWidget(MainWindow);
    centralwidget->setObjectName("centralwidget");
    calendarWidget = new QCalendarWidget(centralwidget);
    calendarWidget->setObjectName("calendarWidget");
    calendarWidget->setGeometry(QRect(540, 0, 256, 190));
    pushButton = new QPushButton(centralwidget);
    pushButton->setObjectName("pushButton");
    pushButton->setGeometry(QRect(30, 20, 75, 24));
    pushButton_2 = new QPushButton(centralwidget);
    pushButton_2->setObjectName("pushButton_2");
    pushButton_2->setGeometry(QRect(120, 20, 75, 24));
    tableView = new QTableView(centralwidget);
    tableView->setObjectName("tableView");
    tableView->setGeometry(QRect(160, 210, 256, 192));
    MainWindow->setCentralWidget(centralwidget);
    menubar = new QMenuBar(MainWindow);
    menubar->setObjectName("menubar");
    menubar->setGeometry(QRect(0, 0, 800, 22));
    MainWindow->setMenuBar(menubar);
    statusbar = new QStatusBar(MainWindow);
    statusbar->setObjectName("statusbar");
    MainWindow->setStatusBar(statusbar);

    retranslateUi(MainWindow);

    QMetaObject::connectSlotsByName(MainWindow);
  } // setupUi

  void retranslateUi(QMainWindow *MainWindow) {
    MainWindow->setWindowTitle(
        QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
    pushButton->setText(
        QCoreApplication::translate("MainWindow", "OpenBase", nullptr));
    pushButton_2->setText(
        QCoreApplication::translate("MainWindow", "SaveBase", nullptr));
  } // retranslateUi
};

namespace Ui {
class MainWindow : public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // JOURNAL_APPUNPCSU_H
