#include "mainwindow.hpp"

#include "ui_journal_app.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColor>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>

#include <QHash>
#include <algorithm>
#include <memory>

#include "AttendanceCellWidget.hpp"
#include "CopyUsersDialog.hpp"
#include "DayMarkerDialog.hpp"
#include "EventApp.hpp"
#include "EventDirectoryDialog.hpp"
#include "EventSqliteStorage.hpp"
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

constexpr int kDateRow = 0;
constexpr int kWeekdayRow = 1;
constexpr int kFirstParticipantRow = 2;
constexpr int kNameColumn = 0;
constexpr int kRankColumn = 1;
constexpr int kAttendanceCountColumn = 2;
constexpr int kFirstDayColumn = 3;

QString applicationDataFilePath(const QString& fileName)
{
  const QString dataDirectory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  return dataDirectory.isEmpty()
             ? QString()
             : QDir(dataDirectory).filePath(fileName);
}

const QString& journalDatabasePath()
{
  static const QString path = []()
  {
    const QString legacyPath =
        QFileInfo(QString::fromUtf8(DB_FILENAME)).absoluteFilePath();
    const QString stablePath =
        applicationDataFilePath(QString::fromUtf8(DB_FILENAME));
    if (QFileInfo::exists(legacyPath) || stablePath.isEmpty())
    {
      return legacyPath;
    }
    if (QFileInfo::exists(stablePath))
    {
      return stablePath;
    }
    return stablePath;
  }();
  return path;
}

QString eventDatabasePath()
{
  return QFileInfo(journalDatabasePath())
      .dir()
      .filePath(EVENT_DB_FILENAME);
}

bool ensureDatabaseDirectory(const QString& databasePath)
{
  return !databasePath.isEmpty() &&
         QDir().mkpath(QFileInfo(databasePath).absolutePath());
}

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
    : QMainWindow(parent), ui(new Ui::MainWindow), modeIndicator_(nullptr),
      localStorageAction_(nullptr), remoteStorageAction_(nullptr),
      serverUrlAction_(nullptr), addParticipantAction_(nullptr),
      removeParticipantAction_(nullptr), configureMonthAction_(nullptr),
      copyParticipantsAction_(nullptr), participantsAction_(nullptr),
      readLocalAction_(nullptr), saveMonthAction_(nullptr),
      pushMonthAction_(nullptr), pullMonthAction_(nullptr),
      tournamentsAction_(nullptr), configuredServerUrl_(),
      activeStorageMode_(),
      activeServerUrl_(), isConnectingStorage_(false), syncInProgress_(false),
      refreshInProgress_(false), monthDataValid_(false),
      monthSetupPromptOpen_(false), monthSetupRequestId_(0),
      dismissedMonthSetupYear_(0), dismissedMonthSetupMonth_(0),
      baseTableWidget(nullptr), activeDays_(), day_in_month(0), month(0),
      year(0)
{
  // setupUi создаёт виджеты из header, который CMake AUTOUIC генерирует из
  // journal_app.ui при сборке.
  ui->setupUi(this);
  setMinimumSize(QSize(1050, 720));
  resize(QSize(1200, 780));

  // Подготавливаем пустой UI-каркас таблиц до загрузки данных из БД.
  createEmptyTable();

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  configuredServerUrl_ =
      env.value("JOURNAL_SERVER_URL", JOURNAL_DEFAULT_SERVER_URL);
  setupMenus();
  const QString storageMode =
      env.value("JOURNAL_STORAGE_MODE", JOURNAL_DEFAULT_STORAGE_MODE).toLower();

  // Начальное подключение выбирается из переменных окружения.
  // Если не задано, используем local.
  isConnectingStorage_ = true;
  updateEditControlsByMode();
  const bool initialStorageReady =
      setupStorage(storageMode == "server" ? "server" : "local",
                   configuredServerUrl_);
  isConnectingStorage_ = false;
  updateEditControlsByMode();
  if (!initialStorageReady)
  {
    ui->statusbar->showMessage(
        "Storage не подключен. Выберите Local или Remote.");
    updateModeIndicator();
  }
  else
  {
    // active* поля нужны отдельно от journalApp_, чтобы UI понимал текущий
    // режим и не делал лишние reconnect при повторном выборе того же источника.
    activeStorageMode_ = (storageMode == "server") ? "server" : "local";
    activeServerUrl_ = activeStorageMode_ == "server"
                           ? configuredServerUrl_
                           : QString();
    updateModeIndicator();
    updateEditControlsByMode();
    refreshMonth();
  }

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

  QHash<QString, ParticipantProfile> profilesById;
  if (journalApp_)
  {
    const auto profiles = journalApp_->participantProfiles(true);
    if (profiles.has_value())
    {
      for (const ParticipantProfile& profile : *profiles)
      {
        profilesById.insert(profile.id.value, profile);
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
  tableWidget->setRowCount(kFirstParticipantRow);
  tableWidget->setColumnCount(kFirstDayColumn +
                              static_cast<int>(activeDays_.size()));

  tableWidget->setItem(kDateRow, kNameColumn, new QTableWidgetItem("Дата"));
  tableWidget->setItem(kWeekdayRow, kNameColumn, new QTableWidgetItem("День"));
  tableWidget->setHorizontalHeaderItem(kNameColumn,
                                       new QTableWidgetItem("Участник"));
  tableWidget->setHorizontalHeaderItem(kRankColumn,
                                       new QTableWidgetItem("Звание"));
  tableWidget->setHorizontalHeaderItem(kAttendanceCountColumn,
                                       new QTableWidgetItem("Посещено"));

  for (int index = 0; index < activeDays_.size(); ++index)
  {
    const int day = activeDays_.at(index);
    const int column = index + kFirstDayColumn;
    const QString dateLabel = QString("%1.%2")
                                  .arg(day, 2, 10, QLatin1Char('0'))
                                  .arg(month, 2, 10, QLatin1Char('0'));
    tableWidget->setItem(kDateRow, column, new QTableWidgetItem(dateLabel));

    const QDate date(static_cast<int>(year), static_cast<int>(month),
                     static_cast<int>(day));
    tableWidget->setItem(
        kWeekdayRow, column,
        new QTableWidgetItem(kDaysOfWeek[date.dayOfWeek() - 1]));
    tableWidget->setHorizontalHeaderItem(column, new QTableWidgetItem(" "));
  }

  QHash<QString, QHash<int, bool>> marksByParticipant;
  for (const AttendanceRecord& record : snapshot.attendance)
  {
    marksByParticipant[record.participantId.value][record.day] =
        record.isChecked;
  }
  QHash<QString, QHash<int, ParticipantDayMarker>> dayMarkersByParticipant;
  for (const ParticipantDayMarker& marker : snapshot.dayMarkers)
  {
    dayMarkersByParticipant[marker.participantId.value].insert(marker.day,
                                                               marker);
  }

  std::vector<Participant> sortedParticipants = snapshot.participants;
  std::stable_sort(
      sortedParticipants.begin(), sortedParticipants.end(),
      [&profilesById](const Participant& lhs, const Participant& rhs)
      {
        const ParticipantRank lhsRank = profilesById.value(lhs.id.value).rank;
        const ParticipantRank rhsRank = profilesById.value(rhs.id.value).rank;
        const int lhsKey = ParticipantRankSortKey(lhsRank);
        const int rhsKey = ParticipantRankSortKey(rhsRank);
        return lhsKey != rhsKey ? lhsKey < rhsKey
                                : QString::localeAwareCompare(
                                      lhs.displayName, rhs.displayName) < 0;
      });

  std::optional<ParticipantRank> currentRank;
  for (const Participant& participant : sortedParticipants)
  {
    const ParticipantProfile profile = profilesById.value(participant.id.value);
    const ParticipantRank rank = profile.rank;
    const QColor groupColor = ParticipantRankSortKey(rank) % 2 == 0
                                  ? QColor(245, 248, 252)
                                  : QColor(235, 241, 248);
    if (!currentRank.has_value() || *currentRank != rank)
    {
      currentRank = rank;
      const int groupRow = tableWidget->rowCount();
      tableWidget->insertRow(groupRow);
      tableWidget->setSpan(groupRow, kNameColumn, 1,
                           tableWidget->columnCount());
      auto* groupItem = new QTableWidgetItem(ParticipantRankDisplayName(rank));
      QFont groupFont = groupItem->font();
      groupFont.setBold(true);
      groupItem->setFont(groupFont);
      groupItem->setBackground(groupColor.darker(105));
      groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsEditable);
      tableWidget->setItem(groupRow, kNameColumn, groupItem);
    }

    const int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    auto* nameItem = new QTableWidgetItem(
        profile.archived ? QString("[архив] %1").arg(participant.displayName)
                         : participant.displayName);
    nameItem->setData(Qt::UserRole, participant.id.value);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setBackground(groupColor);
    tableWidget->setItem(row, kNameColumn, nameItem);
    auto* rankItem = new QTableWidgetItem(ParticipantRankDisplayName(rank));
    rankItem->setFlags(rankItem->flags() & ~Qt::ItemIsEditable);
    rankItem->setBackground(groupColor);
    tableWidget->setItem(row, kRankColumn, rankItem);

    auto* countItem = new QTableWidgetItem();
    countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
    countItem->setTextAlignment(Qt::AlignCenter);
    tableWidget->setItem(row, kAttendanceCountColumn, countItem);

    for (int index = 0; index < activeDays_.size(); ++index)
    {
      const int day = activeDays_.at(index);
      const int column = index + kFirstDayColumn;
      const bool checked =
          marksByParticipant.value(participant.id.value).value(day, false);
      std::optional<ParticipantDayMarker> marker;
      const auto participantMarkers =
          dayMarkersByParticipant.constFind(participant.id.value);
      if (participantMarkers != dayMarkersByParticipant.cend())
      {
        const auto dayMarker = participantMarkers->constFind(day);
        if (dayMarker != participantMarkers->cend())
        {
          marker = dayMarker.value();
        }
      }
      addAttendanceCell(tableWidget, row, column, checked, participant, day,
                        marker);
    }
    updateAttendanceCount(tableWidget, row);
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

  // Читаем только колонки активных дат. Служебные колонки не сериализуются.
  for (int row = kFirstParticipantRow; row < tableWidget->rowCount(); ++row)
  {
    const QTableWidgetItem* idItem = tableWidget->item(row, kNameColumn);
    const ParticipantId participantId{
        idItem ? idItem->data(Qt::UserRole).toString() : QString()};
    if (!participantId.isValid())
    {
      continue;
    }

    for (int index = 0; index < activeDays_.size(); ++index)
    {
      const int day = activeDays_.at(index);
      const int column = index + kFirstDayColumn;

      const auto* cell = qobject_cast<AttendanceCellWidget*>(
          tableWidget->cellWidget(row, column));

      data.push_back({participantId, day, cell ? cell->isChecked() : false});
    }
  }

  return data;
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
  QTableWidget* tableWidget = new QTableWidget(
      kFirstParticipantRow,
      kFirstDayColumn + static_cast<int>(tableDays.size()), this);
  tableWidget->setObjectName("bigTable");

  tableWidget->setHorizontalHeaderItem(kNameColumn,
                                       new QTableWidgetItem("Участник"));
  tableWidget->setHorizontalHeaderItem(kRankColumn,
                                       new QTableWidgetItem("Звание"));
  tableWidget->setHorizontalHeaderItem(kAttendanceCountColumn,
                                       new QTableWidgetItem("Посещено"));
  for (int i = 0; i < tableDays.size(); ++i)
  {
    tableWidget->setHorizontalHeaderItem(i + kFirstDayColumn,
                                         new QTableWidgetItem(" "));
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
          [this, tableWidget](int row, int column)
          {
            if (row < kFirstParticipantRow ||
                (column != kNameColumn && column != kRankColumn))
            {
              return;
            }
            const QTableWidgetItem* item = tableWidget->item(row, kNameColumn);
            const ParticipantId id{item ? item->data(Qt::UserRole).toString()
                                        : QString()};
            if (id.isValid())
            {
              openParticipantProfile(id);
            }
          });

  baseTableWidget = tableWidget;
}

void MainWindow::setupMenus()
{
  QMenuBar* bar = menuBar();
  bar->clear();

  auto* connectionMenu = bar->addMenu("Подключение");
  connectionMenu->setObjectName("connectionMenu");
  localStorageAction_ = connectionMenu->addAction("Локальная база");
  localStorageAction_->setObjectName("localStorageAction");
  localStorageAction_->setCheckable(true);
  remoteStorageAction_ = connectionMenu->addAction("Сервер…");
  remoteStorageAction_->setObjectName("remoteStorageAction");
  remoteStorageAction_->setCheckable(true);
  auto* storageActions = new QActionGroup(this);
  storageActions->setExclusionPolicy(
      QActionGroup::ExclusionPolicy::ExclusiveOptional);
  storageActions->addAction(localStorageAction_);
  storageActions->addAction(remoteStorageAction_);
  connectionMenu->addSeparator();
  serverUrlAction_ =
      connectionMenu->addAction("Адрес сервера для обмена…");
  serverUrlAction_->setObjectName("serverUrlAction");

  auto* monthMenu = bar->addMenu("Месяц");
  monthMenu->setObjectName("monthMenu");
  addParticipantAction_ = monthMenu->addAction("Добавить участника…");
  addParticipantAction_->setObjectName("addParticipantAction");
  addParticipantAction_->setShortcut(QKeySequence::New);
  removeParticipantAction_ =
      monthMenu->addAction("Убрать выбранного участника");
  removeParticipantAction_->setObjectName("removeParticipantAction");
  monthMenu->addSeparator();
  configureMonthAction_ = monthMenu->addAction("Настроить даты учёта…");
  configureMonthAction_->setObjectName("configureMonthAction");
  copyParticipantsAction_ =
      monthMenu->addAction("Перенести участников…");
  copyParticipantsAction_->setObjectName("copyParticipantsAction");
  copyParticipantsAction_->setStatusTip(
      "Добавить участников из другого месяца и при необходимости перенести "
      "расписание по дням недели");

  auto* dataMenu = bar->addMenu("Данные");
  dataMenu->setObjectName("dataMenu");
  readLocalAction_ = dataMenu->addAction("Перечитать локальную базу");
  readLocalAction_->setObjectName("readLocalAction");
  readLocalAction_->setShortcut(QKeySequence::Refresh);
  saveMonthAction_ = dataMenu->addAction("Сохранить месяц");
  saveMonthAction_->setObjectName("saveMonthAction");
  saveMonthAction_->setShortcut(QKeySequence::Save);
  dataMenu->addSeparator();
  pushMonthAction_ = dataMenu->addAction("Отправить месяц на сервер");
  pushMonthAction_->setObjectName("pushMonthAction");
  pullMonthAction_ = dataMenu->addAction("Получить месяц с сервера");
  pullMonthAction_->setObjectName("pullMonthAction");

  auto* directoriesMenu = bar->addMenu("Справочники");
  directoriesMenu->setObjectName("directoriesMenu");
  participantsAction_ = directoriesMenu->addAction("Все участники…");
  participantsAction_->setObjectName("participantsAction");
  tournamentsAction_ = directoriesMenu->addAction("Турниры…");
  tournamentsAction_->setObjectName("tournamentsAction");
  const QString eventsPath = eventDatabasePath();
  tournamentsAction_->setStatusTip(
      eventsPath.isEmpty()
          ? "Открыть отдельную локальную базу турниров"
          : QString("Открыть базу турниров: %1")
                .arg(QDir::toNativeSeparators(eventsPath)));

  modeIndicator_ = new QLabel(this);
  modeIndicator_->setObjectName("storageModeIndicator");
  ui->statusbar->addPermanentWidget(modeIndicator_);

  setConfiguredServerUrl(configuredServerUrl_);
  connect(localStorageAction_, &QAction::triggered, this,
          [this]() { connectLocalStorage(); });
  connect(remoteStorageAction_, &QAction::triggered, this,
          [this]() { connectRemoteStorage(); });
  connect(serverUrlAction_, &QAction::triggered, this,
          [this]() { configureServerUrl(); });
  connect(addParticipantAction_, &QAction::triggered, this,
          [this]() { addParticipantToMonth(); });
  connect(removeParticipantAction_, &QAction::triggered, this,
          [this]() { removeSelectedParticipantFromMonth(); });
  connect(configureMonthAction_, &QAction::triggered, this,
          [this]() { configureMonthDays(); });
  connect(copyParticipantsAction_, &QAction::triggered, this,
          [this]() { copyUsersFromMonth(); });
  connect(readLocalAction_, &QAction::triggered, this,
          [this]() { readLocalMonthToTable(); });
  connect(saveMonthAction_, &QAction::triggered, this,
          [this]() { saveCurrentMonth(); });
  connect(pushMonthAction_, &QAction::triggered, this,
          [this]() { pushCurrentMonthToServer(); });
  connect(pullMonthAction_, &QAction::triggered, this,
          [this]() { pullCurrentMonthFromServer(); });
  connect(participantsAction_, &QAction::triggered, this,
          [this]() { openParticipantDirectory(); });
  connect(tournamentsAction_, &QAction::triggered, this,
          [this]() { openEventDirectory(); });
  updateModeIndicator();
}

void MainWindow::addParticipantToMonth()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_ || activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage("Данные месяца не готовы");
    return;
  }

  bool accepted = false;
  const QString name =
      QInputDialog::getText(this, "Добавление участника", "Имя / ФИО:",
                            QLineEdit::Normal, QString(), &accepted)
          .trimmed();
  if (!accepted)
  {
    return;
  }
  if (name.isEmpty())
  {
    ui->statusbar->showMessage("Введите имя участника");
    return;
  }
  if (!journalApp_ || !journalApp_->addUser(name))
  {
    ui->statusbar->showMessage("Не удалось добавить участника");
    return;
  }

  refreshMonth();
  if (monthDataValid_)
  {
    ui->statusbar->showMessage("Участник добавлен");
  }
}

void MainWindow::removeSelectedParticipantFromMonth()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_ || activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage("Данные месяца не готовы");
    return;
  }

  QTableWidget* table = findChild<QTableWidget*>("bigTable");
  const int row = table ? table->currentRow() : -1;
  if (row < kFirstParticipantRow)
  {
    ui->statusbar->showMessage("Выберите строку участника");
    return;
  }
  const QTableWidgetItem* nameItem = table->item(row, kNameColumn);
  const ParticipantId id{
      nameItem ? nameItem->data(Qt::UserRole).toString() : QString()};
  if (!id.isValid())
  {
    ui->statusbar->showMessage("Выберите строку участника");
    return;
  }
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
}

void MainWindow::saveCurrentMonth()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_ || activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage("Сохранение запрещено: месяц не загружен");
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
}

std::optional<QString> MainWindow::requestServerUrl(const QString& title)
{
  bool accepted = false;
  QString serverUrl = QInputDialog::getText(
      this, title, "Server URL:", QLineEdit::Normal,
      configuredServerUrl_.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL)
                                     : configuredServerUrl_,
      &accepted);
  if (!accepted)
  {
    return std::nullopt;
  }
  serverUrl = serverUrl.trimmed();
  return serverUrl.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL)
                             : serverUrl;
}

void MainWindow::setConfiguredServerUrl(const QString& serverUrl)
{
  configuredServerUrl_ = serverUrl.trimmed().isEmpty()
                             ? QString(JOURNAL_DEFAULT_SERVER_URL)
                             : serverUrl.trimmed();
  const QString target = QString("Сервер: %1").arg(configuredServerUrl_);
  serverUrlAction_->setStatusTip(target);
  remoteStorageAction_->setStatusTip(
      QString("Подключиться к %1").arg(configuredServerUrl_));
  pushMonthAction_->setStatusTip(
      QString("Отправить месяц в %1").arg(configuredServerUrl_));
  pullMonthAction_->setStatusTip(
      QString("Получить месяц из %1").arg(configuredServerUrl_));
}

void MainWindow::configureServerUrl()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  const auto serverUrl = requestServerUrl("Адрес сервера для обмена");
  if (!serverUrl.has_value())
  {
    return;
  }
  setConfiguredServerUrl(*serverUrl);
  ui->statusbar->showMessage(
      QString("Адрес сервера: %1").arg(configuredServerUrl_), 4000);
}

void MainWindow::openEventDirectory()
{
  const QString databasePath = eventDatabasePath();
  if (!ensureDatabaseDirectory(databasePath))
  {
    QMessageBox::warning(this, "Ошибка БД турниров",
                         "Не удалось создать каталог данных приложения.");
    return;
  }
  auto storage = std::make_unique<EventSqliteStorage>();
  if (!storage->open(databasePath))
  {
    QMessageBox::warning(
        this, "Ошибка БД турниров",
        QString("%1\n\nФайл: %2")
            .arg(storage->lastError(),
                 QDir::toNativeSeparators(databasePath)));
    return;
  }
  std::vector<ParticipantProfile> profiles;
  if (journalApp_)
  {
    const auto loaded = journalApp_->participantProfiles(true);
    if (loaded.has_value())
    {
      profiles = *loaded;
    }
    else
    {
      QMessageBox::warning(
          this, "Общий список недоступен",
          "Не удалось загрузить участников журнала. Турниры откроются, "
          "но выбирать наших участников для новых записей нельзя.");
    }
  }
  EventApp eventApp(std::move(storage));
  EventDirectoryDialog dialog(eventApp, std::move(profiles), this);
  dialog.exec();
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
  const QString databasePath = journalDatabasePath();
  if (!ensureDatabaseDirectory(databasePath))
  {
    QMessageBox::warning(this, "Ошибка локальной базы",
                         "Не удалось создать каталог данных приложения.");
    return false;
  }
  if (sqlite.open(databasePath))
  {
    return true;
  }
  const QString error = sqlite.lastError().isEmpty()
                            ? "Не удалось открыть локальную базу"
                            : sqlite.lastError();
  QMessageBox::warning(
      this, "Ошибка локальной базы",
      QString("%1\n\nФайл: %2")
          .arg(error, QDir::toNativeSeparators(databasePath)));
  return false;
}
void MainWindow::connectLocalStorage()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    // Защита от повторного клика, пока предыдущее подключение еще не
    // завершилось.
    updateModeIndicator();
    return;
  }

  if (journalApp_ && activeStorageMode_ == "local")
  {
    ui->statusbar->showMessage("Уже подключено к local storage.", 3000);
    updateModeIndicator();
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
    updateModeIndicator();
    updateEditControlsByMode();
    refreshMonth();
  }
  else
  {
    updateModeIndicator();
    // Сохраняем текущее рабочее подключение при ошибке reconnect.
    if (journalApp_)
    {
      ui->statusbar->showMessage(
          "Переподключение не выполнено, старое подключение сохранено.",
          5000);
    }
  }
}

void MainWindow::connectRemoteStorage()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    updateModeIndicator();
    return;
  }

  const auto requestedUrl = requestServerUrl("Подключение к серверу");
  if (!requestedUrl.has_value())
  {
    updateModeIndicator();
    return;
  }
  setConfiguredServerUrl(*requestedUrl);
  const QString serverUrl = configuredServerUrl_;

  if (journalApp_ && activeStorageMode_ == "server" &&
      activeServerUrl_ == serverUrl)
  {
    ui->statusbar->showMessage("Уже подключено к этому remote storage.", 3000);
    updateModeIndicator();
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
    updateModeIndicator();
    updateEditControlsByMode();
    refreshMonth();
  }
  else
  {
    updateModeIndicator();
    if (journalApp_)
    {
      ui->statusbar->showMessage(
          "Переподключение не выполнено, старое подключение сохранено.",
          5000);
    }
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

void MainWindow::copyUsersFromMonth(bool copyWeekdayPatternByDefault)
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  if (activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage(
        "Перенос участников доступен только в local режиме.", 5000);
    return;
  }

  if (!journalApp_)
  {
    ui->statusbar->showMessage("Сервис не инициализирован");
    return;
  }

  updateCalendarVariables(ui->calendarWidget);
  CopyUsersDialog dialog(static_cast<int>(year), static_cast<int>(month), this,
                         copyWeekdayPatternByDefault);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }

  const bool applySourceWeekdays = dialog.copyWeekdayPattern();
  const CopyScheduleMode scheduleMode =
      applySourceWeekdays ? CopyScheduleMode::ApplySourceWeekdays
                          : CopyScheduleMode::KeepTargetDates;
  const CopyUsersResult result = journalApp_->copyUsersFromMonth(
      dialog.sourceYear(), dialog.sourceMonth(), static_cast<int>(year),
      static_cast<int>(month), scheduleMode);

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
        QString("Перенос завершен. Добавлено: %1, пропущено: %2. %3")
            .arg(result.copied)
            .arg(result.skipped)
            .arg(applySourceWeekdays ? "Расписание применено по дням недели."
                                     : "Расписание источника не применялось."),
        5000);
  }
}

void MainWindow::updateModeIndicator()
{
  if (!modeIndicator_ || !localStorageAction_ || !remoteStorageAction_)
  {
    return;
  }

  localStorageAction_->setChecked(activeStorageMode_ == "local");
  remoteStorageAction_->setChecked(activeStorageMode_ == "server");

  if (activeStorageMode_ == "server")
  {
    modeIndicator_->setText("REMOTE · только чтение");
    modeIndicator_->setStyleSheet("QLabel#storageModeIndicator {"
                                  "  padding: 2px 6px;"
                                  "  border: 1px solid #ad7f00;"
                                  "  background: #fff4cc;"
                                  "  border-radius: 4px;"
                                  "  font-weight: 600;"
                                  "}");
    return;
  }

  if (activeStorageMode_ == "local")
  {
    modeIndicator_->setText("LOCAL");
    modeIndicator_->setStyleSheet("QLabel#storageModeIndicator {"
                                  "  padding: 2px 6px;"
                                  "  border: 1px solid #2b7a0b;"
                                  "  background: #ddffdd;"
                                  "  border-radius: 4px;"
                                  "  font-weight: 600;"
                                  "}");
    return;
  }

  modeIndicator_->setText("НЕТ ПОДКЛЮЧЕНИЯ");
  modeIndicator_->setStyleSheet("QLabel#storageModeIndicator {"
                                "  padding: 2px 6px;"
                                "  border: 1px solid #666;"
                                "  background: #efefef;"
                                "  border-radius: 4px;"
                                "  font-weight: 600;"
                                "}");
}

void MainWindow::updateEditControlsByMode()
{
  const bool controlsIdle =
      !isConnectingStorage_ && !syncInProgress_ && !refreshInProgress_;
  const bool canEditMonth =
      activeStorageMode_ != "server" && monthDataValid_ && controlsIdle;
  addParticipantAction_->setEnabled(canEditMonth);
  removeParticipantAction_->setEnabled(canEditMonth);
  configureMonthAction_->setEnabled(canEditMonth);
  copyParticipantsAction_->setEnabled(canEditMonth);
  saveMonthAction_->setEnabled(canEditMonth);
  pushMonthAction_->setEnabled(canEditMonth);
  pullMonthAction_->setEnabled(canEditMonth);
  participantsAction_->setEnabled(controlsIdle && journalApp_ != nullptr);
  tournamentsAction_->setEnabled(controlsIdle);
  readLocalAction_->setEnabled(controlsIdle);
  serverUrlAction_->setEnabled(controlsIdle);
  localStorageAction_->setEnabled(controlsIdle);
  remoteStorageAction_->setEnabled(controlsIdle);
  ui->calendarWidget->setEnabled(controlsIdle);

  QTableWidget* tableWidget = findChild<QTableWidget*>("bigTable");
  if (tableWidget)
  {
    for (int row = kFirstParticipantRow; row < tableWidget->rowCount(); ++row)
    {
      for (int index = 0; index < activeDays_.size(); ++index)
      {
        const int column = index + kFirstDayColumn;
        auto* cell = qobject_cast<AttendanceCellWidget*>(
            tableWidget->cellWidget(row, column));
        if (cell)
        {
          cell->setEditable(canEditMonth);
        }
      }
    }
  }
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
  const QString serverUrl = configuredServerUrl_.isEmpty()
                                ? QString(JOURNAL_DEFAULT_SERVER_URL)
                                : configuredServerUrl_;

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
  const QString serverUrl = configuredServerUrl_.isEmpty()
                                ? QString(JOURNAL_DEFAULT_SERVER_URL)
                                : configuredServerUrl_;

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

void MainWindow::addAttendanceCell(
    QTableWidget* tableWidget, int row, int column, bool isChecked,
    const Participant& participant, int day,
    const std::optional<ParticipantDayMarker>& marker)
{
  const QDate date(static_cast<int>(year), static_cast<int>(month), day);
  auto* cell = new AttendanceCellWidget(isChecked, participant.displayName,
                                        date, marker, tableWidget);
  tableWidget->setCellWidget(row, column, cell);
  connect(cell->attendanceCheckBox(), &QCheckBox::toggled, tableWidget,
          [this, tableWidget, row](bool)
          { updateAttendanceCount(tableWidget, row); });
  connect(cell->markerButton(), &QToolButton::clicked, cell,
          [this, cell, participant, day]()
          { editDayMarker(cell, participant, day); });
}

//---------------------------------------------------------------

void MainWindow::editDayMarker(AttendanceCellWidget* cell,
                               const Participant& participant, int day)
{
  if (!cell || !journalApp_ || activeStorageMode_ == "server" ||
      isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_)
  {
    ui->statusbar->showMessage("Редактирование отметки сейчас недоступно");
    return;
  }

  const DayMarkerKinds initialKinds =
      cell->marker().has_value() ? cell->marker()->kinds : DayMarkerKinds();
  const QString initialNote =
      cell->marker().has_value() ? cell->marker()->note : QString();
  const QDate date(static_cast<int>(year), static_cast<int>(month), day);
  DayMarkerDialog dialog(participant.displayName, date, initialKinds,
                         initialNote, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }

  if (dialog.clearRequested())
  {
    if (!journalApp_->removeDayMarker(static_cast<int>(year),
                                      static_cast<int>(month), participant.id,
                                      day))
    {
      ui->statusbar->showMessage("Не удалось удалить отметку дня", 5000);
      return;
    }
    cell->setMarker(std::nullopt);
    ui->statusbar->showMessage("Отметка дня удалена", 3000);
    return;
  }

  const ParticipantDayMarker marker{participant.id, day, dialog.selectedKinds(),
                                    dialog.note()};
  if (!journalApp_->saveDayMarker(static_cast<int>(year),
                                  static_cast<int>(month), marker))
  {
    ui->statusbar->showMessage("Не удалось сохранить отметку дня", 5000);
    return;
  }
  cell->setMarker(marker);
  ui->statusbar->showMessage("Отметка дня сохранена", 3000);
}

//---------------------------------------------------------------

void MainWindow::updateAttendanceCount(QTableWidget* tableWidget, int row)
{
  if (!tableWidget || row < kFirstParticipantRow ||
      row >= tableWidget->rowCount())
  {
    return;
  }

  QHash<int, bool> attendanceByDay;
  attendanceByDay.reserve(activeDays_.size());
  for (int index = 0; index < activeDays_.size(); ++index)
  {
    const int column = index + kFirstDayColumn;
    const auto* cell = qobject_cast<AttendanceCellWidget*>(
        tableWidget->cellWidget(row, column));
    attendanceByDay.insert(activeDays_.at(index), cell && cell->isChecked());
  }

  QTableWidgetItem* countItem = tableWidget->item(row, kAttendanceCountColumn);
  if (!countItem)
  {
    countItem = new QTableWidgetItem();
    countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
    countItem->setTextAlignment(Qt::AlignCenter);
    tableWidget->setItem(row, kAttendanceCountColumn, countItem);
  }
  countItem->setText(
      QString::number(CountCheckedActiveDays(activeDays_, attendanceByDay)));
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
  calendarWidget->setMinimumSize(QSize(200, 280));
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
