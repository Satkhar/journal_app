#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDate>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>
#include <QHash>

#include "JournalLocal.hpp"
#include "SqliteConnect.hpp"
#include "config.h"

//---------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      baseTableWidget(nullptr),
      day_in_month(0),
      month(0),
      year(0) {
  ui->setupUi(this);

  // Подготавливаем пустой UI-каркас таблиц до загрузки данных из БД.
  createEmptyTable();
  createCheckTable();

  // Собираем рабочую цепочку: SQLite -> локальное хранилище -> use-case слой.
  auto sqlite = std::make_unique<SqliteConnect>();
  if (!sqlite->open(DB_PATH)) {
    QMessageBox::critical(this, "Ошибка", "Не удалось подключиться к базе данных.");
    return;
  }

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  journalApp_ = std::make_unique<JournalApp>(std::move(local));

  // Добавление пользователя за текущий месяц (месяц хранится в JournalApp).
  connect(ui->btnAdd, &QPushButton::clicked, this, [this]() {
    const QString name = ui->lineEdit->text().trimmed();
    if (name.isEmpty()) {
      ui->statusbar->showMessage("Введите имя");
      return;
    }

    if (!journalApp_ || !journalApp_->addUser(name)) {
      ui->statusbar->showMessage("Не удалось добавить пользователя");
      return;
    }

    refreshMonth();
    ui->statusbar->showMessage("Пользователь добавлен");
  });

  // Удаление пользователя за текущий месяц.
  connect(ui->btnDel, &QPushButton::clicked, this, [this]() {
    const QString name = ui->lineEdit->text().trimmed();
    if (name.isEmpty()) {
      ui->statusbar->showMessage("Введите имя");
      return;
    }

    if (!journalApp_ || !journalApp_->deleteUser(name)) {
      ui->statusbar->showMessage("Не удалось удалить пользователя");
      return;
    }

    refreshMonth();
    ui->statusbar->showMessage("Пользователь удален");
  });

  // Явное обновление данных на экране из БД.
  connect(ui->btnReadBase, &QPushButton::clicked, this, [this]() {
    refreshMonth();
    ui->statusbar->showMessage("Данные обновлены");
  });

  // Сохраняем текущее состояние чекбоксов таблицы в БД.
  connect(ui->btnSaveCurTable, &QPushButton::clicked, this, [this]() {
    if (!journalApp_) {
      ui->statusbar->showMessage("Сервис не инициализирован");
      return;
    }

    const auto data = collectMonthFromTable();
    if (!journalApp_->saveMonth(static_cast<int>(year), static_cast<int>(month), data)) {
      ui->statusbar->showMessage("Ошибка сохранения");
      return;
    }

    ui->statusbar->showMessage("Таблица сохранена");
  });

  // При смене страницы календаря пересоздаем сетку дней и загружаем месяц.
  connect(ui->calendarWidget, &QCalendarWidget::currentPageChanged, this,
          [this](int shownYear, int shownMonth) {
            Q_UNUSED(shownYear)
            Q_UNUSED(shownMonth)
            createEmptyTable();
            refreshMonth();
          });

  refreshMonth();
}

//---------------------------------------------------------------

MainWindow::~MainWindow() { delete ui; }

//---------------------------------------------------------------

void MainWindow::refreshMonth() {
  if (!journalApp_) {
    return;
  }

  // Синхронизируем внутренние переменные месяца и перечитываем snapshot из БД.
  updateCalendarVariables(ui->calendarWidget);
  const MonthSnapshot snapshot =
      journalApp_->loadMonth(static_cast<int>(year), static_cast<int>(month));
  renderMonth(snapshot);
}

//---------------------------------------------------------------

void MainWindow::renderMonth(const MonthSnapshot& snapshot) {
  QTableWidget* tableWidget = findChild<QTableWidget*>("bigTable");
  if (!tableWidget) {
    return;
  }

  // Первые две строки зарезервированы под "Дата" и "День недели".
  tableWidget->clearContents();
  tableWidget->setRowCount(2);

  tableWidget->setItem(0, 0, new QTableWidgetItem("Дата"));
  tableWidget->setItem(1, 0, new QTableWidgetItem("День"));

  for (uint32_t day = 1; day <= day_in_month; ++day) {
    const QString dateLabel = QString("%1.%2")
                                  .arg(day, 2, 10, QLatin1Char('0'))
                                  .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, static_cast<int>(day) + 1, new QTableWidgetItem(dateLabel));

    const QDate date(static_cast<int>(year), static_cast<int>(month), static_cast<int>(day));
    tableWidget->setItem(1, static_cast<int>(day) + 1,
                         new QTableWidgetItem(kDaysOfWeek[date.dayOfWeek() - 1]));
  }

  // Быстрый индекс: имя пользователя -> набор отмеченных дней.
  QHash<QString, QHash<int, bool>> markByUser;
  for (const AttendanceRecord& record : snapshot.attendance) {
    markByUser[record.userName][record.day] = record.isChecked;
  }

  // Ниже отрисовываем строки пользователей и их отметки по дням.
  for (const QString& user : snapshot.users) {
    const int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    tableWidget->setItem(row, 0, new QTableWidgetItem(""));
    tableWidget->setItem(row, 1, new QTableWidgetItem(user));

    for (uint32_t day = 1; day <= day_in_month; ++day) {
      bool checked = false;
      if (markByUser.contains(user) && markByUser[user].contains(static_cast<int>(day))) {
        checked = markByUser[user][static_cast<int>(day)];
      }
      addCheckBox(tableWidget, row, static_cast<int>(day) + 1, checked);
    }
  }

  tableWidget->resizeColumnsToContents();
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> MainWindow::collectMonthFromTable() const {
  std::vector<AttendanceRecord> data;

  QTableWidget* tableWidget = ui->centralwidget->findChild<QTableWidget*>("bigTable");
  if (!tableWidget) {
    return data;
  }

  // Читаем все пользовательские строки (строки 0-1 служебные).
  for (int row = 2; row < tableWidget->rowCount(); ++row) {
    const QTableWidgetItem* nameItem = tableWidget->item(row, 1);
    if (!nameItem) {
      continue;
    }

    const QString name = nameItem->text().trimmed();
    if (name.isEmpty()) {
      continue;
    }

    // Для каждого пользователя сохраняем отметки по всем дням месяца.
    for (int day = 1; day <= static_cast<int>(day_in_month); ++day) {
      QCheckBox* checkBox =
          qobject_cast<QCheckBox*>(tableWidget->cellWidget(row, day + 1));

      data.push_back({name, day, checkBox ? checkBox->isChecked() : false});
    }
  }

  return data;
}

//---------------------------------------------------------------

void MainWindow::createCheckTable() {
  // Скрытая таблица-заготовка чекбоксов по дням недели (используется как вспомогательная).
  QTableWidget* tableWidget = new QTableWidget(1, 7, this);
  tableWidget->setHorizontalHeaderLabels(kDaysOfWeek);
  tableWidget->setVerticalHeaderLabels({" "});
  tableWidget->setObjectName("checkTable");

  for (int col = 0; col < tableWidget->columnCount(); ++col) {
    QCheckBox* checkBox = new QCheckBox();
    checkBox->setChecked(col < 5);
    tableWidget->setCellWidget(0, col, checkBox);
  }

  tableWidget->resizeColumnsToContents();
  tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  tableWidget->setVisible(false);

  QVBoxLayout* layout = ui->centralwidget->findChild<QVBoxLayout*>("verticalLayout_3");
  if (layout) {
    layout->addWidget(tableWidget);
  }
}

//---------------------------------------------------------------

void MainWindow::createEmptyTable() {
  // Пересоздаем основную таблицу под выбранный месяц (2 служебные строки + дни).
  updateCalendarVariables(ui->calendarWidget);

  QTableWidget* oldTable = findChild<QTableWidget*>("bigTable");
  if (oldTable) {
    delete oldTable;
  }

  QTableWidget* tableWidget = new QTableWidget(2, 2 + static_cast<int>(day_in_month), this);
  tableWidget->setObjectName("bigTable");

  tableWidget->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
  tableWidget->setHorizontalHeaderItem(1, new QTableWidgetItem("Name"));
  for (uint32_t i = 0; i < day_in_month; ++i) {
    tableWidget->setHorizontalHeaderItem(static_cast<int>(i) + 2, new QTableWidgetItem(" "));
  }

  tableWidget->resizeColumnsToContents();
  tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

  QGridLayout* layout = ui->centralwidget->findChild<QGridLayout*>("gridLayout");
  if (layout) {
    layout->addWidget(tableWidget);
  }

  baseTableWidget = tableWidget;
}

//---------------------------------------------------------------

int MainWindow::searchDate(QTableWidget* tableWidget, const QString& dateLabel) const {
  const int startColumn = 2;
  for (int column = startColumn; column < static_cast<int>(day_in_month) + startColumn;
       ++column) {
    QTableWidgetItem* dataItem = tableWidget->item(0, column);
    if (dataItem && dataItem->text() == dateLabel) {
      return column;
    }
  }
  return 0;
}

//---------------------------------------------------------------

void MainWindow::addCheckBox(QTableWidget* tableWidget, int row, int column,
                             bool is_checked) {
  // Ячейка посещаемости всегда представлена чекбоксом.
  QCheckBox* checkBox = new QCheckBox();
  checkBox->setChecked(is_checked);
  tableWidget->setCellWidget(row, column, checkBox);
}

//---------------------------------------------------------------

void MainWindow::updateCalendarVariables(QCalendarWidget* calendarWidget) {
  // Единая точка обновления параметров текущего месяца из календаря UI.
  month = static_cast<uint32_t>(calendarWidget->monthShown());
  year = static_cast<uint32_t>(calendarWidget->yearShown());
  day_in_month = static_cast<uint32_t>(QDate(static_cast<int>(year), static_cast<int>(month), 1)
                                           .daysInMonth());

  calendarWidget->setMinimumSize(QSize(200, 150));
  calendarWidget->setMaximumSize(QSize(400, 300));
  calendarWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
}

//---------------------------------------------------------------
