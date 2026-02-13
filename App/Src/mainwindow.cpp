#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDate>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProcessEnvironment>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>
#include <QHash>

#include "JournalLocal.hpp"
#include "JournalRemote.hpp"
#include "SqliteConnect.hpp"
#include "SyncService.hpp"
#include "config.h"

//---------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      modeBadgeLabel_(nullptr),
      serverUrlEdit_(nullptr),
      connectLocalBtn_(nullptr),
      connectRemoteBtn_(nullptr),
      activeStorageMode_(),
      activeServerUrl_(),
      isConnectingStorage_(false),
      baseTableWidget(nullptr),
      day_in_month(0),
      month(0),
      year(0) {
  ui->setupUi(this);

  // Подготавливаем пустой UI-каркас таблиц до загрузки данных из БД.
  createEmptyTable();
  createCheckTable();
  setupStorageControls();

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString serverUrl =
      env.value("JOURNAL_SERVER_URL", JOURNAL_DEFAULT_SERVER_URL);
  serverUrlEdit_->setText(serverUrl);
  const QString storageMode =
      env.value("JOURNAL_STORAGE_MODE", JOURNAL_DEFAULT_STORAGE_MODE).toLower();

  if (!setupStorage(storageMode == "server" ? "server" : "local",
                    serverUrlEdit_->text().trimmed())) {
    ui->statusbar->showMessage("Storage не подключен. Выберите Local или Remote.");
    updateModeBadge();
  } else {
    activeStorageMode_ = (storageMode == "server") ? "server" : "local";
    activeServerUrl_ = serverUrlEdit_->text().trimmed();
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  }

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
    readLocalMonthToTable();
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

  // Отправка локального состояния месяца на сервер.
  connect(ui->btnCreateTable, &QPushButton::clicked, this, [this]() {
    pushCurrentMonthToServer();
  });

  // Подтянуть месяц с сервера в локальную БД.
  connect(ui->btnPullServer, &QPushButton::clicked, this, [this]() {
    pullCurrentMonthFromServer();
  });

  // При смене страницы календаря пересоздаем сетку дней и загружаем месяц.
  connect(ui->calendarWidget, &QCalendarWidget::currentPageChanged, this,
          [this](int shownYear, int shownMonth) {
            Q_UNUSED(shownYear)
            Q_UNUSED(shownMonth)
            createEmptyTable();
            refreshMonth();
          });

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

void MainWindow::setupStorageControls() {
  QVBoxLayout* layout = ui->centralwidget->findChild<QVBoxLayout*>("verticalLayout_3");
  if (!layout) {
    return;
  }

  auto* storagePanelLayout = new QVBoxLayout();
  auto* controlsLayout = new QHBoxLayout();
  controlsLayout->addWidget(new QLabel("Server URL:", this));

  serverUrlEdit_ = new QLineEdit(this);
  serverUrlEdit_->setPlaceholderText("http://127.0.0.1:8080");
  controlsLayout->addWidget(serverUrlEdit_);

  connectLocalBtn_ = new QPushButton("Local", this);
  controlsLayout->addWidget(connectLocalBtn_);

  connectRemoteBtn_ = new QPushButton("Remote", this);
  controlsLayout->addWidget(connectRemoteBtn_);

  modeBadgeLabel_ = new QLabel("Mode: DISCONNECTED", this);
  modeBadgeLabel_->setObjectName("modeBadgeLabel");
  modeBadgeLabel_->setMinimumWidth(250);
  modeBadgeLabel_->setMaximumWidth(250);
  modeBadgeLabel_->setStyleSheet(
      "QLabel#modeBadgeLabel {"
      "  padding: 4px 8px;"
      "  border: 1px solid #666;"
      "  border-radius: 6px;"
      "  font-weight: 600;"
      "}");

  storagePanelLayout->addLayout(controlsLayout);
  storagePanelLayout->addWidget(modeBadgeLabel_, 0, Qt::AlignLeft);
  layout->insertLayout(1, storagePanelLayout);

  connect(connectLocalBtn_, &QPushButton::clicked, this,
          [this]() { connectLocalFromUi(); });
  connect(connectRemoteBtn_, &QPushButton::clicked, this,
          [this]() { connectRemoteFromUi(); });
}

bool MainWindow::setupStorage(const QString& mode, const QString& serverUrl) {
  if (mode == "server") {
    const QString targetUrl =
        serverUrl.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL) : serverUrl;

    auto remote = std::make_unique<JournalRemote>(targetUrl, JOURNAL_REMOTE_TIMEOUT_MS);
    QString error;
    if (!remote->connect(&error)) {
      QMessageBox::warning(this, "Ошибка подключения",
                           QString("Не удалось подключиться к серверу: %1")
                               .arg(error.isEmpty() ? targetUrl : error));
      return false;
    }

    journalApp_ = std::make_unique<JournalApp>(std::move(remote), false);
    ui->statusbar->showMessage(QString("Режим: server (%1)").arg(targetUrl), 5000);
    updateEditControlsByMode();
    return true;
  }

  auto sqlite = std::make_unique<SqliteConnect>();
  if (!sqlite->open(DB_PATH)) {
    QMessageBox::warning(this, "Ошибка подключения",
                         "Не удалось подключиться к локальной базе данных.");
    return false;
  }

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  journalApp_ = std::make_unique<JournalApp>(std::move(local), true);
  ui->statusbar->showMessage("Режим: local", 3000);
  updateEditControlsByMode();
  return true;
}

void MainWindow::connectLocalFromUi() {
  if (isConnectingStorage_) {
    return;
  }

  if (journalApp_ && activeStorageMode_ == "local") {
    ui->statusbar->showMessage("Уже подключено к local storage.", 3000);
    return;
  }

  isConnectingStorage_ = true;
  if (connectLocalBtn_) {
    connectLocalBtn_->setEnabled(false);
  }
  if (connectRemoteBtn_) {
    connectRemoteBtn_->setEnabled(false);
  }

  const bool ok = setupStorage("local", QString());
  if (connectLocalBtn_) {
    connectLocalBtn_->setEnabled(true);
  }
  if (connectRemoteBtn_) {
    connectRemoteBtn_->setEnabled(true);
  }
  isConnectingStorage_ = false;

  if (ok) {
    activeStorageMode_ = "local";
    activeServerUrl_.clear();
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  } else if (journalApp_) {
    // Сохраняем текущее рабочее подключение при ошибке reconnect.
    ui->statusbar->showMessage("Переподключение не выполнено, старое подключение сохранено.", 5000);
  }
}

void MainWindow::connectRemoteFromUi() {
  if (isConnectingStorage_) {
    return;
  }

  const QString serverUrlRaw = serverUrlEdit_ ? serverUrlEdit_->text().trimmed() : QString();
  const QString serverUrl =
      serverUrlRaw.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL) : serverUrlRaw;

  if (journalApp_ && activeStorageMode_ == "server" && activeServerUrl_ == serverUrl) {
    ui->statusbar->showMessage("Уже подключено к этому remote storage.", 3000);
    return;
  }

  isConnectingStorage_ = true;
  if (connectLocalBtn_) {
    connectLocalBtn_->setEnabled(false);
  }
  if (connectRemoteBtn_) {
    connectRemoteBtn_->setEnabled(false);
  }

  const bool ok = setupStorage("server", serverUrl);
  if (connectLocalBtn_) {
    connectLocalBtn_->setEnabled(true);
  }
  if (connectRemoteBtn_) {
    connectRemoteBtn_->setEnabled(true);
  }
  isConnectingStorage_ = false;

  if (ok) {
    activeStorageMode_ = "server";
    activeServerUrl_ = serverUrl;
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  } else if (journalApp_) {
    ui->statusbar->showMessage("Переподключение не выполнено, старое подключение сохранено.", 5000);
  }
}

void MainWindow::updateModeBadge() {
  if (!modeBadgeLabel_) {
    return;
  }

  if (activeStorageMode_ == "server") {
    modeBadgeLabel_->setText("Mode: REMOTE (read-only)");
    modeBadgeLabel_->setStyleSheet(
        "QLabel#modeBadgeLabel {"
        "  padding: 4px 8px;"
        "  border: 1px solid #ad7f00;"
        "  background: #fff4cc;"
        "  border-radius: 6px;"
        "  font-weight: 600;"
        "}");
    return;
  }

  if (activeStorageMode_ == "local") {
    modeBadgeLabel_->setText("Mode: LOCAL");
    modeBadgeLabel_->setStyleSheet(
        "QLabel#modeBadgeLabel {"
        "  padding: 4px 8px;"
        "  border: 1px solid #2b7a0b;"
        "  background: #ddffdd;"
        "  border-radius: 6px;"
        "  font-weight: 600;"
        "}");
    return;
  }

  modeBadgeLabel_->setText("Mode: DISCONNECTED");
  modeBadgeLabel_->setStyleSheet(
      "QLabel#modeBadgeLabel {"
      "  padding: 4px 8px;"
      "  border: 1px solid #666;"
      "  background: #efefef;"
      "  border-radius: 6px;"
      "  font-weight: 600;"
      "}");
}

void MainWindow::updateEditControlsByMode() {
  const bool isLocalMode = (activeStorageMode_ != "server");
  ui->btnAdd->setEnabled(isLocalMode);
  ui->btnDel->setEnabled(isLocalMode);
  ui->btnSaveCurTable->setEnabled(isLocalMode);
  ui->btnCreateTable->setEnabled(isLocalMode);
  ui->btnPullServer->setEnabled(isLocalMode);
  ui->lineEdit->setEnabled(isLocalMode);
}

void MainWindow::readLocalMonthToTable() {
  auto sqlite = std::make_unique<SqliteConnect>();
  if (!sqlite->open(DB_PATH)) {
    ui->statusbar->showMessage("Read Base error: не удалось открыть локальную БД.", 6000);
    return;
  }

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  JournalApp localReader(std::move(local), true);

  updateCalendarVariables(ui->calendarWidget);
  const MonthSnapshot snapshot =
      localReader.loadMonth(static_cast<int>(year), static_cast<int>(month));
  renderMonth(snapshot);
  ui->statusbar->showMessage("Read Base: локальные данные загружены.", 4000);
}

void MainWindow::pushCurrentMonthToServer() {
  if (activeStorageMode_ != "local") {
    ui->statusbar->showMessage(
        "Push доступен только из local режима. Переключитесь на Local.", 5000);
    return;
  }

  if (!journalApp_) {
    ui->statusbar->showMessage("Локальное хранилище не подключено.", 5000);
    return;
  }

  const QString serverUrl =
      serverUrlEdit_ && !serverUrlEdit_->text().trimmed().isEmpty()
          ? serverUrlEdit_->text().trimmed()
          : QString(JOURNAL_DEFAULT_SERVER_URL);

  QString error;
  updateCalendarVariables(ui->calendarWidget);
  const auto data = collectMonthFromTable();
  SyncService sync(JOURNAL_REMOTE_TIMEOUT_MS);
  if (!sync.pushMonthToServer(serverUrl, static_cast<int>(year), static_cast<int>(month), data,
                              &error)) {
    ui->statusbar->showMessage(
        QString("Push error: %1")
            .arg(error.isEmpty() ? "не удалось сохранить месяц на сервер" : error),
        6000);
    return;
  }

  ui->statusbar->showMessage(QString("Push OK -> %1").arg(serverUrl), 5000);
}

void MainWindow::pullCurrentMonthFromServer() {
  if (activeStorageMode_ != "local") {
    ui->statusbar->showMessage(
        "Pull доступен только из local режима. Переключитесь на Local.", 5000);
    return;
  }

  const QString serverUrl =
      serverUrlEdit_ && !serverUrlEdit_->text().trimmed().isEmpty()
          ? serverUrlEdit_->text().trimmed()
          : QString(JOURNAL_DEFAULT_SERVER_URL);

  auto sqlite = std::make_unique<SqliteConnect>();
  if (!sqlite->open(DB_PATH)) {
    ui->statusbar->showMessage("Pull error: не удалось открыть локальную БД.", 6000);
    return;
  }
  JournalLocal local(std::move(sqlite));

  QString error;
  updateCalendarVariables(ui->calendarWidget);
  SyncService sync(JOURNAL_REMOTE_TIMEOUT_MS);
  if (!sync.pullMonthToLocal(serverUrl, static_cast<int>(year), static_cast<int>(month), local,
                             &error)) {
    ui->statusbar->showMessage(
        QString("Pull error: %1")
            .arg(error.isEmpty() ? "не удалось получить месяц с сервера" : error),
        6000);
    return;
  }

  refreshMonth();
  ui->statusbar->showMessage(QString("Pull OK <- %1").arg(serverUrl), 5000);
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
