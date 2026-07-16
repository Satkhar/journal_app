#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDate>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <QHash>
#include <memory>

#include "CopyUsersDialog.hpp"
#include "JournalLocal.hpp"
#include "JournalRemote.hpp"
#include "MonthDaysDialog.hpp"
#include "ParticipantDialog.hpp"
#include "ParticipantDirectoryDialog.hpp"
#include "SqliteConnect.hpp"
#include "SyncService.hpp"
#include "config.h"

//---------------------------------------------------------------

namespace
{

QVector<int> fullMonthDays(int year, int month)
{
  QVector<int> days;
  const int maxDay = QDate(year, month, 1).daysInMonth();
  days.reserve(maxDay);
  for (int day = 1; day <= maxDay; ++day)
  {
    days.push_back(day);
  }
  return days;
}

} // namespace

//---------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), connectionGroup_(nullptr),
      monthGroup_(nullptr), dataGroup_(nullptr), modeBadgeLabel_(nullptr),
      serverUrlEdit_(nullptr), connectLocalBtn_(nullptr),
      connectRemoteBtn_(nullptr), configureMonthBtn_(nullptr),
      copyUsersBtn_(nullptr), participantsBtn_(nullptr), activeStorageMode_(),
      activeServerUrl_(), isConnectingStorage_(false), syncInProgress_(false),
      refreshInProgress_(false), monthDataValid_(false),
      monthSetupPromptOpen_(false), monthSetupRequestId_(0),
      dismissedMonthSetupYear_(0), dismissedMonthSetupMonth_(0),
      baseTableWidget(nullptr), activeDays_(), day_in_month(0), month(0),
      year(0)
{
  // setupUi создает виджеты из сгенерированного файла journal_app.h.
  ui->setupUi(this);
  setMinimumSize(QSize(1050, 720));
  resize(QSize(1200, 780));

  // Подготавливаем пустой UI-каркас таблиц до загрузки данных из БД.
  createEmptyTable();
  createCheckTable();
  // Панели действий: подключение, текущий месяц, данные.
  setupActionPanels();

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString serverUrl =
      env.value("JOURNAL_SERVER_URL", JOURNAL_DEFAULT_SERVER_URL);
  serverUrlEdit_->setText(serverUrl);
  const QString storageMode =
      env.value("JOURNAL_STORAGE_MODE", JOURNAL_DEFAULT_STORAGE_MODE).toLower();

  // Начальное подключение выбирается из переменных окружения.
  // Если не задано, используем local.
  isConnectingStorage_ = true;
  updateEditControlsByMode();
  const bool initialStorageReady =
      setupStorage(storageMode == "server" ? "server" : "local",
                   serverUrlEdit_->text().trimmed());
  isConnectingStorage_ = false;
  updateEditControlsByMode();
  if (!initialStorageReady)
  {
    ui->statusbar->showMessage(
        "Storage не подключен. Выберите Local или Remote.");
    updateModeBadge();
  }
  else
  {
    // active* поля нужны отдельно от journalApp_, чтобы UI понимал текущий
    // режим и не делал лишние reconnect при повторном выборе того же источника.
    activeStorageMode_ = (storageMode == "server") ? "server" : "local";
    activeServerUrl_ = serverUrlEdit_->text().trimmed();
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  }

  // Добавление пользователя за текущий месяц (месяц хранится в JournalApp).
  connect(ui->btnAdd, &QPushButton::clicked, this,
          [this]()
          {
            if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
                !monthDataValid_)
            {
              ui->statusbar->showMessage("Данные месяца не готовы");
              return;
            }
            const QString name = ui->lineEdit->text().trimmed();
            if (name.isEmpty())
            {
              ui->statusbar->showMessage("Введите имя");
              return;
            }

            // addUser работает только в local режиме (см.
            // updateEditControlsByMode()).
            if (!journalApp_ || !journalApp_->addUser(name))
            {
              ui->statusbar->showMessage("Не удалось добавить пользователя");
              return;
            }

            refreshMonth();
            if (monthDataValid_)
            {
              ui->statusbar->showMessage("Пользователь добавлен");
            }
          });

  // Удаление пользователя за текущий месяц.
  connect(
      ui->btnDel, &QPushButton::clicked, this,
      [this]()
      {
        if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
            !monthDataValid_)
        {
          ui->statusbar->showMessage("Данные месяца не готовы");
          return;
        }
        QTableWidget* table = findChild<QTableWidget*>("bigTable");
        const int row = table ? table->currentRow() : -1;
        if (row < 2)
        {
          ui->statusbar->showMessage("Выберите строку участника");
          return;
        }

        const QTableWidgetItem* idItem = table->item(row, 0);
        const ParticipantId id{idItem ? idItem->data(Qt::UserRole).toString()
                                      : QString()};
        if (!journalApp_ || !journalApp_->removeParticipant(id))
        {
          ui->statusbar->showMessage("Не удалось убрать участника из месяца");
          return;
        }

        refreshMonth();
        if (monthDataValid_)
        {
          ui->statusbar->showMessage("Участник убран из месяца");
        }
      });

  // Явное обновление данных на экране из БД.
  connect(ui->btnReadBase, &QPushButton::clicked, this,
          [this]()
          {
            // Важное правило UX: Read Base всегда читает local БД, независимо
            // от active режима.
            readLocalMonthToTable();
          });

  // Сохраняем текущее состояние чекбоксов таблицы в БД.
  connect(ui->btnSaveCurTable, &QPushButton::clicked, this,
          [this]()
          {
            if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
                !monthDataValid_)
            {
              ui->statusbar->showMessage(
                  "Сохранение запрещено: месяц не загружен");
              return;
            }
            if (!journalApp_)
            {
              ui->statusbar->showMessage("Сервис не инициализирован");
              return;
            }

            const auto data = collectMonthFromTable();
            if (!journalApp_->saveAttendance(static_cast<int>(year),
                                             static_cast<int>(month), data))
            {
              ui->statusbar->showMessage("Ошибка сохранения");
              return;
            }

            ui->statusbar->showMessage("Таблица сохранена");
          });

  // Отправка локального состояния месяца на сервер.
  connect(ui->btnCreateTable, &QPushButton::clicked, this,
          [this]() { pushCurrentMonthToServer(); });

  // Подтянуть месяц с сервера в локальную БД.
  connect(ui->btnPullServer, &QPushButton::clicked, this,
          [this]() { pullCurrentMonthFromServer(); });

  // При смене страницы календаря пересоздаем сетку дней и загружаем месяц.
  connect(ui->calendarWidget, &QCalendarWidget::currentPageChanged, this,
          [this](int shownYear, int shownMonth)
          {
            if (shownYear != dismissedMonthSetupYear_ ||
                shownMonth != dismissedMonthSetupMonth_)
            {
              dismissedMonthSetupYear_ = 0;
              dismissedMonthSetupMonth_ = 0;
            }
            activeDays_.clear();
            createEmptyTable();
            refreshMonth();
          });
}

//---------------------------------------------------------------

MainWindow::~MainWindow()
{
  delete ui;
}

//---------------------------------------------------------------

void MainWindow::refreshMonth()
{
  if (refreshInProgress_)
  {
    return;
  }
  ++monthSetupRequestId_;
  if (!journalApp_)
  {
    monthDataValid_ = false;
    updateEditControlsByMode();
    return;
  }

  // Синхронизируем внутренние переменные месяца и перечитываем snapshot
  // из текущего активного storage.
  updateCalendarVariables(ui->calendarWidget);
  refreshInProgress_ = true;
  updateEditControlsByMode();
  const MonthSnapshot snapshot =
      journalApp_->loadMonth(static_cast<int>(year), static_cast<int>(month));
  refreshInProgress_ = false;
  if (snapshot.state == MonthState::Error)
  {
    monthDataValid_ = false;
    updateEditControlsByMode();
    ui->statusbar->showMessage(
        QString("Ошибка чтения месяца: %1").arg(snapshot.errorMessage), 6000);
    return;
  }
  monthDataValid_ = true;
  renderMonth(snapshot);
  updateEditControlsByMode();
  scheduleMonthSetup(snapshot);
}

void MainWindow::scheduleMonthSetup(const MonthSnapshot& snapshot)
{
  const int targetYear = static_cast<int>(year);
  const int targetMonth = static_cast<int>(month);
  if (activeStorageMode_ != "local" || snapshot.state != MonthState::Missing ||
      monthSetupPromptOpen_ ||
      (dismissedMonthSetupYear_ == targetYear &&
       dismissedMonthSetupMonth_ == targetMonth))
  {
    return;
  }

  const quint64 requestId = monthSetupRequestId_;
  QTimer::singleShot(
      0, this,
      [this, requestId, targetYear, targetMonth]()
      {
        if (requestId != monthSetupRequestId_ || monthSetupPromptOpen_ ||
            isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
            activeStorageMode_ != "local" || !journalApp_)
        {
          return;
        }
        updateCalendarVariables(ui->calendarWidget);
        if (static_cast<int>(year) != targetYear ||
            static_cast<int>(month) != targetMonth)
        {
          return;
        }
        const MonthStateResult state =
            journalApp_->getMonthState(targetYear, targetMonth);
        if (state.state == MonthState::Error)
        {
          monthDataValid_ = false;
          updateEditControlsByMode();
          ui->statusbar->showMessage(
              QString("Ошибка проверки месяца: %1").arg(state.errorMessage),
              6000);
          return;
        }
        if (state.state == MonthState::Ready)
        {
          refreshMonth();
          return;
        }
        showMonthSetupMenu(targetYear, targetMonth);
      });
}

void MainWindow::showMonthSetupMenu(int targetYear, int targetMonth)
{
  monthSetupPromptOpen_ = true;
  QMessageBox menu(this);
  menu.setObjectName("monthSetupMenu");
  menu.setWindowTitle("Создание месяца");
  menu.setIcon(QMessageBox::Question);
  menu.setText(QString("Месяц %1.%2 ещё не создан.")
                   .arg(targetMonth, 2, 10, QLatin1Char('0'))
                   .arg(targetYear));
  menu.setInformativeText("Выберите способ заполнения месяца.");
  QPushButton* createButton =
      menu.addButton("Создать с нуля", QMessageBox::AcceptRole);
  QPushButton* copyButton =
      menu.addButton("Перенести участников", QMessageBox::ActionRole);
  QPushButton* laterButton = menu.addButton("Позже", QMessageBox::RejectRole);
  createButton->setObjectName("createMonthFromScratchButton");
  copyButton->setObjectName("copyMonthUsersButton");
  laterButton->setObjectName("dismissMonthSetupButton");
  menu.setDefaultButton(createButton);
  menu.setEscapeButton(laterButton);
  menu.exec();

  if (menu.clickedButton() == createButton)
  {
    configureMonthDays();
  }
  else if (menu.clickedButton() == copyButton)
  {
    copyUsersFromMonth(true);
  }
  else
  {
    dismissedMonthSetupYear_ = targetYear;
    dismissedMonthSetupMonth_ = targetMonth;
  }
  monthSetupPromptOpen_ = false;
}

//---------------------------------------------------------------

void MainWindow::renderMonth(const MonthSnapshot& snapshot)
{
  QTableWidget* tableWidget = findChild<QTableWidget*>("bigTable");
  if (!tableWidget)
  {
    return;
  }

  QSet<QString> archivedIds;
  if (journalApp_)
  {
    const auto profiles = journalApp_->participantProfiles(true);
    if (profiles.has_value())
    {
      for (const ParticipantProfile& profile : *profiles)
      {
        if (profile.archived)
        {
          archivedIds.insert(profile.id.value);
        }
      }
    }
  }

  activeDays_ = snapshot.activeDays;
  if (activeDays_.isEmpty())
  {
    activeDays_ =
        fullMonthDays(static_cast<int>(year), static_cast<int>(month));
  }

  // Всегда заново рисуем служебные строки и все user-строки,
  // чтобы таблица была полностью консистентна snapshot.
  tableWidget->clearContents();
  tableWidget->setRowCount(2);
  tableWidget->setColumnCount(2 + activeDays_.size());

  tableWidget->setItem(0, 0, new QTableWidgetItem("Дата"));
  tableWidget->setItem(1, 0, new QTableWidgetItem("День"));
  tableWidget->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
  tableWidget->setHorizontalHeaderItem(1, new QTableWidgetItem("Name"));

  for (int index = 0; index < activeDays_.size(); ++index)
  {
    const int day = activeDays_.at(index);
    const int column = index + 2;
    const QString dateLabel = QString("%1.%2")
                                  .arg(day, 2, 10, QLatin1Char('0'))
                                  .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(0, column, new QTableWidgetItem(dateLabel));

    const QDate date(static_cast<int>(year), static_cast<int>(month),
                     static_cast<int>(day));
    tableWidget->setItem(
        1, column, new QTableWidgetItem(kDaysOfWeek[date.dayOfWeek() - 1]));
    tableWidget->setHorizontalHeaderItem(column, new QTableWidgetItem(" "));
  }

  QHash<QString, QHash<int, bool>> marksByParticipant;
  for (const AttendanceRecord& record : snapshot.attendance)
  {
    marksByParticipant[record.participantId.value][record.day] =
        record.isChecked;
  }

  for (const Participant& participant : snapshot.participants)
  {
    const int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    auto* idItem = new QTableWidgetItem(participant.id.value.left(8));
    idItem->setData(Qt::UserRole, participant.id.value);
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    tableWidget->setItem(row, 0, idItem);
    auto* nameItem = new QTableWidgetItem(
        archivedIds.contains(participant.id.value)
            ? QString("[архив] %1").arg(participant.displayName)
            : participant.displayName);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    tableWidget->setItem(row, 1, nameItem);

    for (int index = 0; index < activeDays_.size(); ++index)
    {
      const int day = activeDays_.at(index);
      const int column = index + 2;
      const bool checked =
          marksByParticipant.value(participant.id.value).value(day, false);
      addCheckBox(tableWidget, row, column, checked);
    }
  }

  tableWidget->resizeColumnsToContents();
}

//---------------------------------------------------------------

std::vector<AttendanceRecord> MainWindow::collectMonthFromTable() const
{
  std::vector<AttendanceRecord> data;

  QTableWidget* tableWidget =
      ui->centralwidget->findChild<QTableWidget*>("bigTable");
  if (!tableWidget)
  {
    return data;
  }

  // Читаем все пользовательские строки (строки 0-1 служебные).
  for (int row = 2; row < tableWidget->rowCount(); ++row)
  {
    const QTableWidgetItem* idItem = tableWidget->item(row, 0);
    const ParticipantId participantId{
        idItem ? idItem->data(Qt::UserRole).toString() : QString()};
    if (!participantId.isValid())
    {
      continue;
    }

    for (int column = 2; column < tableWidget->columnCount(); ++column)
    {
      const QTableWidgetItem* dateItem = tableWidget->item(0, column);
      if (!dateItem)
      {
        continue;
      }

      const int day = dateItem->text().left(2).toInt();
      if (day < 1 || day > static_cast<int>(day_in_month))
      {
        continue;
      }

      QCheckBox* checkBox =
          qobject_cast<QCheckBox*>(tableWidget->cellWidget(row, column));

      data.push_back(
          {participantId, day, checkBox ? checkBox->isChecked() : false});
    }
  }

  return data;
}

//---------------------------------------------------------------

void MainWindow::createCheckTable()
{
  // Скрытая таблица-заготовка чекбоксов по дням недели (используется как
  // вспомогательная).
  QTableWidget* tableWidget = new QTableWidget(1, 7, this);
  tableWidget->setHorizontalHeaderLabels(kDaysOfWeek);
  tableWidget->setVerticalHeaderLabels({" "});
  tableWidget->setObjectName("checkTable");

  for (int col = 0; col < tableWidget->columnCount(); ++col)
  {
    QCheckBox* checkBox = new QCheckBox();
    checkBox->setChecked(col < 5);
    tableWidget->setCellWidget(0, col, checkBox);
  }

  tableWidget->resizeColumnsToContents();
  tableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  tableWidget->setVisible(false);

  QVBoxLayout* layout =
      ui->centralwidget->findChild<QVBoxLayout*>("verticalLayout_3");
  if (layout)
  {
    layout->addWidget(tableWidget);
  }
}

//---------------------------------------------------------------

void MainWindow::createEmptyTable()
{
  // Пересоздаем основную таблицу под выбранный месяц (2 служебные строки +
  // дни).
  updateCalendarVariables(ui->calendarWidget);

  // При смене месяца/страницы календаря удаляем старую таблицу целиком
  // и создаем новую с нужным числом колонок дней.
  QTableWidget* oldTable = findChild<QTableWidget*>("bigTable");
  if (oldTable)
  {
    delete oldTable;
  }

  const QVector<int> tableDays =
      activeDays_.isEmpty()
          ? fullMonthDays(static_cast<int>(year), static_cast<int>(month))
          : activeDays_;
  QTableWidget* tableWidget = new QTableWidget(2, 2 + tableDays.size(), this);
  tableWidget->setObjectName("bigTable");

  tableWidget->setHorizontalHeaderItem(0, new QTableWidgetItem("ID"));
  tableWidget->setHorizontalHeaderItem(1, new QTableWidgetItem("Name"));
  for (int i = 0; i < tableDays.size(); ++i)
  {
    tableWidget->setHorizontalHeaderItem(i + 2, new QTableWidgetItem(" "));
  }

  tableWidget->resizeColumnsToContents();
  tableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  tableWidget->setMinimumHeight(260);
  tableWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QGridLayout* layout =
      ui->centralwidget->findChild<QGridLayout*>("gridLayout");
  if (layout)
  {
    layout->addWidget(tableWidget, 1, 0);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(0, 1);
  }

  connect(tableWidget, &QTableWidget::cellDoubleClicked, this,
          [this, tableWidget](int row, int)
          {
            if (row < 2)
            {
              return;
            }
            const QTableWidgetItem* item = tableWidget->item(row, 0);
            const ParticipantId id{item ? item->data(Qt::UserRole).toString()
                                        : QString()};
            if (id.isValid())
            {
              openParticipantProfile(id);
            }
          });

  baseTableWidget = tableWidget;
}

void MainWindow::setupActionPanels()
{
  QVBoxLayout* layout =
      ui->centralwidget->findChild<QVBoxLayout*>("verticalLayout_3");
  if (!layout)
  {
    return;
  }

  setupConnectionPanel(layout);
  setupMonthPanel(layout);
  setupDataPanel(layout);
}

void MainWindow::setupConnectionPanel(QVBoxLayout* parentLayout)
{
  connectionGroup_ = new QGroupBox("Подключение", this);
  auto* storagePanelLayout = new QVBoxLayout(connectionGroup_);
  auto* controlsLayout = new QHBoxLayout();
  controlsLayout->addWidget(new QLabel("Server URL:", this));

  serverUrlEdit_ = new QLineEdit(this);
  serverUrlEdit_->setPlaceholderText("http://127.0.0.1:7070");
  controlsLayout->addWidget(serverUrlEdit_);

  connectLocalBtn_ = new QPushButton("Local", this);
  controlsLayout->addWidget(connectLocalBtn_);

  connectRemoteBtn_ = new QPushButton("Remote", this);
  controlsLayout->addWidget(connectRemoteBtn_);

  modeBadgeLabel_ = new QLabel("Mode: DISCONNECTED", this);
  modeBadgeLabel_->setObjectName("modeBadgeLabel");
  modeBadgeLabel_->setMinimumWidth(250);
  modeBadgeLabel_->setMaximumWidth(250);
  modeBadgeLabel_->setStyleSheet("QLabel#modeBadgeLabel {"
                                 "  padding: 4px 8px;"
                                 "  border: 1px solid #666;"
                                 "  border-radius: 6px;"
                                 "  font-weight: 600;"
                                 "}");

  storagePanelLayout->addLayout(controlsLayout);
  storagePanelLayout->addWidget(modeBadgeLabel_, 0, Qt::AlignLeft);
  parentLayout->insertWidget(0, connectionGroup_);

  connect(connectLocalBtn_, &QPushButton::clicked, this,
          [this]() { connectLocalFromUi(); });
  connect(connectRemoteBtn_, &QPushButton::clicked, this,
          [this]() { connectRemoteFromUi(); });
}

void MainWindow::setupMonthPanel(QVBoxLayout* parentLayout)
{
  monthGroup_ = new QGroupBox("Текущий месяц", this);
  auto* monthLayout = new QVBoxLayout(monthGroup_);

  auto* userButtonsLayout = new QHBoxLayout();
  userButtonsLayout->addWidget(ui->btnAdd);
  userButtonsLayout->addWidget(ui->btnDel);
  monthLayout->addLayout(userButtonsLayout);
  monthLayout->addWidget(ui->lineEdit);

  configureMonthBtn_ = new QPushButton("Настроить дни", this);
  monthLayout->addWidget(configureMonthBtn_);

  copyUsersBtn_ = new QPushButton("Перенести пользователей", this);
  monthLayout->addWidget(copyUsersBtn_);

  participantsBtn_ = new QPushButton("Все участники", this);
  monthLayout->addWidget(participantsBtn_);

  parentLayout->insertWidget(1, monthGroup_);

  connect(configureMonthBtn_, &QPushButton::clicked, this,
          [this]() { configureMonthDays(); });
  connect(copyUsersBtn_, &QPushButton::clicked, this,
          [this]() { copyUsersFromMonth(); });
  connect(participantsBtn_, &QPushButton::clicked, this,
          [this]() { openParticipantDirectory(); });
}

void MainWindow::setupDataPanel(QVBoxLayout* parentLayout)
{
  dataGroup_ = new QGroupBox("Данные", this);
  auto* dataLayout = new QVBoxLayout(dataGroup_);

  ui->btnReadBase->setText("Прочитать месяц");
  ui->btnSaveCurTable->setText("Сохранить месяц");
  dataLayout->addWidget(ui->btnReadBase);
  dataLayout->addWidget(ui->btnSaveCurTable);
  dataLayout->addWidget(ui->btnCreateTable);
  dataLayout->addWidget(ui->btnPullServer);

  parentLayout->insertWidget(2, dataGroup_);
}

bool MainWindow::setupStorage(const QString& mode, const QString& serverUrl)
{
  if (mode == "server")
  {
    const QString targetUrl =
        serverUrl.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL) : serverUrl;

    // Remote подключаем только как read-only источник для UI-режима просмотра.
    auto remote =
        std::make_unique<JournalRemote>(targetUrl, JOURNAL_REMOTE_TIMEOUT_MS);
    QString error;
    if (!remote->connect(&error))
    {
      QMessageBox::warning(this, "Ошибка подключения",
                           QString("Не удалось подключиться к серверу: %1")
                               .arg(error.isEmpty() ? targetUrl : error));
      return false;
    }
    journalApp_ = std::make_unique<JournalApp>(std::move(remote));
    monthDataValid_ = false;
    ui->statusbar->showMessage(QString("Режим: server (%1)").arg(targetUrl),
                               5000);
    updateEditControlsByMode();
    return true;
  }

  // Local режим: полноценное чтение/редактирование/сохранение.
  auto sqlite = std::make_unique<SqliteConnect>();
  if (!openLocalDatabase(*sqlite))
  {
    return false;
  }

  // Цепочка владения после этого:
  // journalApp_ -> JournalApp -> JournalLocal -> SqliteConnect.
  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  journalApp_ = std::make_unique<JournalApp>(std::move(local));
  monthDataValid_ = false;
  ui->statusbar->showMessage("Режим: local", 3000);
  updateEditControlsByMode();
  return true;
}

bool MainWindow::openLocalDatabase(SqliteConnect& sqlite)
{
  if (sqlite.open(DB_PATH))
  {
    return true;
  }
  QMessageBox::warning(this, "Ошибка локальной базы",
                       sqlite.lastError().isEmpty()
                           ? "Не удалось открыть локальную базу"
                           : sqlite.lastError());
  return false;
}
void MainWindow::connectLocalFromUi()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    // Защита от повторного клика, пока предыдущее подключение еще не
    // завершилось.
    return;
  }

  if (journalApp_ && activeStorageMode_ == "local")
  {
    ui->statusbar->showMessage("Уже подключено к local storage.", 3000);
    return;
  }

  ++monthSetupRequestId_;
  isConnectingStorage_ = true;
  updateEditControlsByMode();

  const bool ok = setupStorage("local", QString());
  isConnectingStorage_ = false;
  updateEditControlsByMode();

  if (ok)
  {
    activeStorageMode_ = "local";
    activeServerUrl_.clear();
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  }
  else if (journalApp_)
  {
    // Сохраняем текущее рабочее подключение при ошибке reconnect.
    ui->statusbar->showMessage(
        "Переподключение не выполнено, старое подключение сохранено.", 5000);
  }
}

void MainWindow::connectRemoteFromUi()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    return;
  }

  const QString serverUrlRaw =
      serverUrlEdit_ ? serverUrlEdit_->text().trimmed() : QString();
  // Пустой input в UI трактуем как "используй дефолтный сервер из config/env".
  const QString serverUrl = serverUrlRaw.isEmpty()
                                ? QString(JOURNAL_DEFAULT_SERVER_URL)
                                : serverUrlRaw;

  if (journalApp_ && activeStorageMode_ == "server" &&
      activeServerUrl_ == serverUrl)
  {
    ui->statusbar->showMessage("Уже подключено к этому remote storage.", 3000);
    return;
  }

  ++monthSetupRequestId_;
  isConnectingStorage_ = true;
  updateEditControlsByMode();

  const bool ok = setupStorage("server", serverUrl);
  isConnectingStorage_ = false;
  updateEditControlsByMode();

  if (ok)
  {
    activeStorageMode_ = "server";
    activeServerUrl_ = serverUrl;
    updateModeBadge();
    updateEditControlsByMode();
    refreshMonth();
  }
  else if (journalApp_)
  {
    ui->statusbar->showMessage(
        "Переподключение не выполнено, старое подключение сохранено.", 5000);
  }
}

void MainWindow::configureMonthDays()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  if (activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage(
        "Настройка месяца доступна только в local режиме.", 5000);
    return;
  }

  if (!journalApp_)
  {
    ui->statusbar->showMessage("Сервис не инициализирован");
    return;
  }

  updateCalendarVariables(ui->calendarWidget);
  QVector<int> initialDays = activeDays_;
  if (initialDays.isEmpty())
  {
    initialDays =
        fullMonthDays(static_cast<int>(year), static_cast<int>(month));
  }

  MonthDaysDialog dialog(static_cast<int>(year), static_cast<int>(month),
                         initialDays, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }

  const QVector<int> selectedDays = dialog.selectedDays();
  if (!journalApp_->saveActiveDays(static_cast<int>(year),
                                   static_cast<int>(month), selectedDays))
  {
    ui->statusbar->showMessage("Не удалось сохранить настройку месяца.", 6000);
    return;
  }

  activeDays_ = selectedDays;
  refreshMonth();
  if (monthDataValid_)
  {
    ui->statusbar->showMessage("Настройка месяца сохранена.", 4000);
  }
}

void MainWindow::copyUsersFromMonth(bool copyActiveDaysByDefault)
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  if (activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage(
        "Перенос пользователей доступен только в local режиме.", 5000);
    return;
  }

  if (!journalApp_)
  {
    ui->statusbar->showMessage("Сервис не инициализирован");
    return;
  }

  updateCalendarVariables(ui->calendarWidget);
  CopyUsersDialog dialog(static_cast<int>(year), static_cast<int>(month), this,
                         copyActiveDaysByDefault);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }

  const CopyUsersResult result = journalApp_->copyUsersFromMonth(
      dialog.sourceYear(), dialog.sourceMonth(), static_cast<int>(year),
      static_cast<int>(month), dialog.copyActiveDays());

  if (!result.ok)
  {
    ui->statusbar->showMessage(
        QString("Перенос не выполнен: %1").arg(result.errorMessage), 6000);
    return;
  }

  refreshMonth();
  if (monthDataValid_)
  {
    ui->statusbar->showMessage(
        QString("Перенос завершен. Добавлено: %1, пропущено: %2")
            .arg(result.copied)
            .arg(result.skipped),
        5000);
  }
}

void MainWindow::updateModeBadge()
{
  if (!modeBadgeLabel_)
  {
    return;
  }

  // Цвет бейджа отражает не просто источник данных, а разрешенный режим работы
  // UI.
  if (activeStorageMode_ == "server")
  {
    modeBadgeLabel_->setText("Mode: REMOTE (read-only)");
    modeBadgeLabel_->setStyleSheet("QLabel#modeBadgeLabel {"
                                   "  padding: 4px 8px;"
                                   "  border: 1px solid #ad7f00;"
                                   "  background: #fff4cc;"
                                   "  border-radius: 6px;"
                                   "  font-weight: 600;"
                                   "}");
    return;
  }

  if (activeStorageMode_ == "local")
  {
    modeBadgeLabel_->setText("Mode: LOCAL");
    modeBadgeLabel_->setStyleSheet("QLabel#modeBadgeLabel {"
                                   "  padding: 4px 8px;"
                                   "  border: 1px solid #2b7a0b;"
                                   "  background: #ddffdd;"
                                   "  border-radius: 6px;"
                                   "  font-weight: 600;"
                                   "}");
    return;
  }

  modeBadgeLabel_->setText("Mode: DISCONNECTED");
  modeBadgeLabel_->setStyleSheet("QLabel#modeBadgeLabel {"
                                 "  padding: 4px 8px;"
                                 "  border: 1px solid #666;"
                                 "  background: #efefef;"
                                 "  border-radius: 6px;"
                                 "  font-weight: 600;"
                                 "}");
}

void MainWindow::updateEditControlsByMode()
{
  const bool controlsIdle =
      !isConnectingStorage_ && !syncInProgress_ && !refreshInProgress_;
  const bool canEditMonth =
      activeStorageMode_ != "server" && monthDataValid_ && controlsIdle;
  ui->btnAdd->setEnabled(canEditMonth);
  ui->btnDel->setEnabled(canEditMonth);
  ui->btnSaveCurTable->setEnabled(canEditMonth);
  ui->btnCreateTable->setEnabled(canEditMonth);
  ui->btnPullServer->setEnabled(canEditMonth);
  ui->lineEdit->setEnabled(canEditMonth);
  if (configureMonthBtn_)
  {
    configureMonthBtn_->setEnabled(canEditMonth);
  }
  if (copyUsersBtn_)
  {
    copyUsersBtn_->setEnabled(canEditMonth);
  }
  if (participantsBtn_)
  {
    participantsBtn_->setEnabled(controlsIdle && journalApp_ != nullptr);
  }
  ui->btnReadBase->setEnabled(controlsIdle);
  ui->calendarWidget->setEnabled(controlsIdle);
  serverUrlEdit_->setEnabled(controlsIdle);
  connectLocalBtn_->setEnabled(controlsIdle);
  connectRemoteBtn_->setEnabled(controlsIdle);
}

void MainWindow::readLocalMonthToTable()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  // Читаем local БД через отдельный локальный reader, не переключая active
  // storage. Это делает поведение Read Base предсказуемым даже при active
  // remote.
  auto sqlite = std::make_unique<SqliteConnect>();
  if (!openLocalDatabase(*sqlite))
  {
    return;
  }

  auto local = std::make_unique<JournalLocal>(std::move(sqlite));
  // Отдельный JournalApp нужен только как временный use-case для чтения local
  // snapshot.
  JournalApp localReader(std::move(local));

  updateCalendarVariables(ui->calendarWidget);
  const MonthSnapshot snapshot =
      localReader.loadMonth(static_cast<int>(year), static_cast<int>(month));
  if (snapshot.state == MonthState::Error)
  {
    if (activeStorageMode_ == "local")
    {
      monthDataValid_ = false;
      updateEditControlsByMode();
    }
    ui->statusbar->showMessage(
        QString("Read Base error: %1").arg(snapshot.errorMessage), 6000);
    return;
  }
  if (activeStorageMode_ == "local")
  {
    monthDataValid_ = true;
  }
  renderMonth(snapshot);
  updateEditControlsByMode();
  ui->statusbar->showMessage("Read Base: локальные данные загружены.", 4000);
}

void MainWindow::pushCurrentMonthToServer()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_)
  {
    ui->statusbar->showMessage("Другая операция уже выполняется", 3000);
    return;
  }
  if (activeStorageMode_ != "local")
  {
    ui->statusbar->showMessage(
        "Push доступен только из local режима. Переключитесь на Local.", 5000);
    return;
  }

  if (!journalApp_)
  {
    ui->statusbar->showMessage("Локальное хранилище не подключено.", 5000);
    return;
  }

  // Push всегда берет текущее содержимое таблицы, а не перечитывает БД перед
  // отправкой.
  const QString serverUrl =
      serverUrlEdit_ && !serverUrlEdit_->text().trimmed().isEmpty()
          ? serverUrlEdit_->text().trimmed()
          : QString(JOURNAL_DEFAULT_SERVER_URL);

  QString error;
  updateCalendarVariables(ui->calendarWidget);
  MonthSnapshot snapshot =
      journalApp_->loadMonth(static_cast<int>(year), static_cast<int>(month));
  if (snapshot.state == MonthState::Error)
  {
    ui->statusbar->showMessage(snapshot.errorMessage, 6000);
    return;
  }
  snapshot.activeDays = activeDays_;
  snapshot.attendance = collectMonthFromTable();
  SyncService sync(JOURNAL_REMOTE_TIMEOUT_MS);
  syncInProgress_ = true;
  updateEditControlsByMode();
  const bool pushed =
      sync.pushMonthToServer(serverUrl, static_cast<int>(year),
                             static_cast<int>(month), snapshot, &error);
  syncInProgress_ = false;
  updateEditControlsByMode();
  if (!pushed)
  {
    ui->statusbar->showMessage(
        QString("Push error: %1")
            .arg(error.isEmpty() ? "не удалось сохранить месяц на сервер"
                                 : error),
        6000);
    return;
  }

  ui->statusbar->showMessage(QString("Push OK -> %1").arg(serverUrl), 5000);
}

void MainWindow::pullCurrentMonthFromServer()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Другая операция уже выполняется", 3000);
    return;
  }
  if (activeStorageMode_ != "local")
  {
    ui->statusbar->showMessage(
        "Pull доступен только из local режима. Переключитесь на Local.", 5000);
    return;
  }

  // Pull пишет в local DB, поэтому сам UI должен оставаться в local режиме.
  const QString serverUrl =
      serverUrlEdit_ && !serverUrlEdit_->text().trimmed().isEmpty()
          ? serverUrlEdit_->text().trimmed()
          : QString(JOURNAL_DEFAULT_SERVER_URL);

  auto sqlite = std::make_unique<SqliteConnect>();
  if (!openLocalDatabase(*sqlite))
  {
    return;
  }
  // Pull пишет в local storage, поэтому создаем отдельный local adapter.
  JournalLocal local(std::move(sqlite));

  QString error;
  updateCalendarVariables(ui->calendarWidget);
  SyncService sync(JOURNAL_REMOTE_TIMEOUT_MS);
  syncInProgress_ = true;
  updateEditControlsByMode();
  const bool pulled =
      sync.pullMonthToLocal(serverUrl, static_cast<int>(year),
                            static_cast<int>(month), local, &error);
  syncInProgress_ = false;
  updateEditControlsByMode();
  if (!pulled)
  {
    ui->statusbar->showMessage(
        QString("Pull error: %1")
            .arg(error.isEmpty() ? "не удалось получить месяц с сервера"
                                 : error),
        6000);
    return;
  }

  refreshMonth();
  if (monthDataValid_)
  {
    ui->statusbar->showMessage(QString("Pull OK <- %1").arg(serverUrl), 5000);
  }
}

//---------------------------------------------------------------

int MainWindow::searchDate(QTableWidget* tableWidget,
                           const QString& dateLabel) const
{
  const int startColumn = 2;
  // Колонки 0 и 1 заняты служебными полями ID/Name, даты начинаются с 2.
  for (int column = startColumn;
       column < static_cast<int>(day_in_month) + startColumn; ++column)
  {
    QTableWidgetItem* dataItem = tableWidget->item(0, column);
    if (dataItem && dataItem->text() == dateLabel)
    {
      return column;
    }
  }
  return 0;
}

//---------------------------------------------------------------

void MainWindow::addCheckBox(QTableWidget* tableWidget, int row, int column,
                             bool is_checked)
{
  // Ячейка посещаемости всегда представлена чекбоксом.
  QCheckBox* checkBox = new QCheckBox();
  checkBox->setChecked(is_checked);
  tableWidget->setCellWidget(row, column, checkBox);
}

//---------------------------------------------------------------

void MainWindow::updateCalendarVariables(QCalendarWidget* calendarWidget)
{
  // Единая точка обновления параметров текущего месяца из календаря UI.
  month = static_cast<uint32_t>(calendarWidget->monthShown());
  year = static_cast<uint32_t>(calendarWidget->yearShown());
  day_in_month = static_cast<uint32_t>(
      QDate(static_cast<int>(year), static_cast<int>(month), 1).daysInMonth());

  // Размеры календаря нормализуем здесь, чтобы это не дублировалось в
  // UI-сценариях.
  calendarWidget->setMinimumSize(QSize(200, 150));
  calendarWidget->setMaximumSize(QSize(400, 300));
  calendarWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
}

//---------------------------------------------------------------

void MainWindow::openParticipantProfile(const ParticipantId& id)
{
  if (!journalApp_)
  {
    return;
  }
  const auto loaded = journalApp_->participantProfile(id);
  if (!loaded.has_value())
  {
    ui->statusbar->showMessage("Карточка участника не найдена");
    return;
  }

  const bool editable = activeStorageMode_ == "local";
  ParticipantDialog dialog(*loaded, editable, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }
  if (dialog.action() == ParticipantDialog::Action::Save)
  {
    if (!journalApp_->updateParticipantProfile(dialog.profile()))
    {
      QMessageBox::warning(this, "Ошибка",
                           "Не удалось сохранить карточку участника");
      return;
    }
    ui->statusbar->showMessage("Карточка сохранена");
  }
  else if (dialog.action() == ParticipantDialog::Action::ToggleArchive)
  {
    const bool ok = dialog.targetArchived()
                        ? journalApp_->archiveParticipant(id)
                        : journalApp_->restoreParticipant(id);
    if (!ok)
    {
      QMessageBox::warning(this, "Ошибка",
                           "Не удалось изменить статус участника");
      return;
    }
    ui->statusbar->showMessage(dialog.targetArchived()
                                   ? "Участник архивирован"
                                   : "Участник восстановлен");
  }
  refreshMonth();
}

void MainWindow::openParticipantDirectory()
{
  if (!journalApp_)
  {
    return;
  }
  const auto profiles = journalApp_->participantProfiles(true);
  if (!profiles.has_value())
  {
    QMessageBox::warning(this, "Ошибка",
                         "Не удалось прочитать каталог участников");
    return;
  }
  ParticipantDirectoryDialog dialog(*profiles, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }
  const auto id = dialog.selectedId();
  if (id.has_value())
  {
    openParticipantProfile(*id);
  }
}
