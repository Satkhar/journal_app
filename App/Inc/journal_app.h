/********************************************************************************
** Form generated from reading UI file 'journal_appsfZeAE.ui'
**
** Created by: Qt User Interface Compiler version 6.8.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef JOURNAL_APPSFZEAE_H
#define JOURNAL_APPSFZEAE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCalendarWidget>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
 public:
  QWidget *centralwidget;
  QGridLayout *gridLayout;
  QHBoxLayout *horizontalLayout_3;
  QVBoxLayout *verticalLayout_3;
  QHBoxLayout *horizontalLayout_2;
  QVBoxLayout *verticalLayout;
  QHBoxLayout *horizontalLayout;
  QPushButton *btnAdd;
  QPushButton *btnDel;
  QLineEdit *lineEdit;
  QVBoxLayout *verticalLayout_2;
  QPushButton *btnReadBase;
  QPushButton *btnCreateTable;
  QPushButton *btnSaveCurTable;
  QSpacerItem *verticalSpacer;
  QCalendarWidget *calendarWidget;
  QMenuBar *menubar;
  QStatusBar *statusbar;

  void setupUi(QMainWindow *MainWindow)
  {
    if (MainWindow->objectName().isEmpty())
    {
      MainWindow->setObjectName("MainWindow");
    }
    MainWindow->resize(776, 532);
    centralwidget = new QWidget(MainWindow);
    centralwidget->setObjectName("centralwidget");
    gridLayout = new QGridLayout(centralwidget);
    gridLayout->setObjectName("gridLayout");
    horizontalLayout_3 = new QHBoxLayout();
    horizontalLayout_3->setObjectName("horizontalLayout_3");
    verticalLayout_3 = new QVBoxLayout();
    verticalLayout_3->setObjectName("verticalLayout_3");
    horizontalLayout_2 = new QHBoxLayout();
    horizontalLayout_2->setObjectName("horizontalLayout_2");
    verticalLayout = new QVBoxLayout();
    verticalLayout->setObjectName("verticalLayout");
    horizontalLayout = new QHBoxLayout();
    horizontalLayout->setObjectName("horizontalLayout");
    btnAdd = new QPushButton(centralwidget);
    btnAdd->setObjectName("btnAdd");

    horizontalLayout->addWidget(btnAdd);

    btnDel = new QPushButton(centralwidget);
    btnDel->setObjectName("btnDel");

    horizontalLayout->addWidget(btnDel);

    verticalLayout->addLayout(horizontalLayout);

    lineEdit = new QLineEdit(centralwidget);
    lineEdit->setObjectName("lineEdit");

    verticalLayout->addWidget(lineEdit);

    horizontalLayout_2->addLayout(verticalLayout);

    verticalLayout_2 = new QVBoxLayout();
    verticalLayout_2->setObjectName("verticalLayout_2");
    btnReadBase = new QPushButton(centralwidget);
    btnReadBase->setObjectName("btnReadBase");

    verticalLayout_2->addWidget(btnReadBase);

    btnCreateTable = new QPushButton(centralwidget);
    btnCreateTable->setObjectName("btnCreateTable");
    btnCreateTable->setEnabled(false);

    verticalLayout_2->addWidget(btnCreateTable);

    btnSaveCurTable = new QPushButton(centralwidget);
    btnSaveCurTable->setObjectName("btnSaveCurTable");

    verticalLayout_2->addWidget(btnSaveCurTable);

    horizontalLayout_2->addLayout(verticalLayout_2);

    verticalLayout_3->addLayout(horizontalLayout_2);

    verticalSpacer = new QSpacerItem(20, 108, QSizePolicy::Policy::Minimum,
                                     QSizePolicy::Policy::Minimum);

    verticalLayout_3->addItem(verticalSpacer);

    horizontalLayout_3->addLayout(verticalLayout_3);

    calendarWidget = new QCalendarWidget(centralwidget);
    calendarWidget->setObjectName("calendarWidget");

    horizontalLayout_3->addWidget(calendarWidget);

    gridLayout->addLayout(horizontalLayout_3, 0, 0, 1, 1);

    MainWindow->setCentralWidget(centralwidget);
    menubar = new QMenuBar(MainWindow);
    menubar->setObjectName("menubar");
    menubar->setGeometry(QRect(0, 0, 776, 22));
    MainWindow->setMenuBar(menubar);
    statusbar = new QStatusBar(MainWindow);
    statusbar->setObjectName("statusbar");
    MainWindow->setStatusBar(statusbar);

    retranslateUi(MainWindow);

    QMetaObject::connectSlotsByName(MainWindow);
  }  // setupUi

  void retranslateUi(QMainWindow *MainWindow)
  {
    MainWindow->setWindowTitle(
        QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
    btnAdd->setText(
        QCoreApplication::translate("MainWindow", "Add User", nullptr));
    btnDel->setText(
        QCoreApplication::translate("MainWindow", "Del User", nullptr));
    btnReadBase->setText(
        QCoreApplication::translate("MainWindow", "Read Base", nullptr));
    btnCreateTable->setText(QCoreApplication::translate(
        "MainWindow", "Create Month table", nullptr));
    btnSaveCurTable->setText(QCoreApplication::translate(
        "MainWindow", "Save Current Table", nullptr));
  }  // retranslateUi
};

namespace Ui
{
class MainWindow : public Ui_MainWindow
{
};
}  // namespace Ui

QT_END_NAMESPACE

#endif  // JOURNAL_APPSFZEAE_H
