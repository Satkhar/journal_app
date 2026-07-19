#include "mainwindow.hpp"

#include "ui_journal_app.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSignalBlocker>
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

constexpr int kFirstContentRow = 0;
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

QString dayHeaderLabel(int year, int month, int day)
{
  const QDate date(year, month, day);
  if (!date.isValid())
  {
    return {};
  }
  return QString("%1.%2\n%3")
      .arg(day, 2, 10, QLatin1Char('0'))
      .arg(month, 2, 10, QLatin1Char('0'))
      .arg(kDaysOfWeek.at(date.dayOfWeek() - 1));
}

bool environmentFlag(const QProcessEnvironment& environment,
                     const QString& name)
{
  const QString value = environment.value(name).trimmed().toLower();
  return value == "1" || value == "true" || value == "yes";
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
      tournamentsAction_(nullptr), configuredServerUrl_(), serverAuthToken_(),
      allowInsecureServerHttp_(false), allowRemoteSchemaChanges_(false),
      activeStorageMode_(),
      activeServerUrl_(), isConnectingStorage_(false), syncInProgress_(false),
      refreshInProgress_(false), monthDataValid_(false),
      monthDraft_(), monthDraftYear_(0), monthDraftMonth_(0),
      revertingCalendarPage_(false),
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

  setupCalendarControls();

  // Подготавливаем пустой UI-каркас таблиц до загрузки данных из БД.
  createEmptyTable();

  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  configuredServerUrl_ =
      env.value("JOURNAL_SERVER_URL", JOURNAL_DEFAULT_SERVER_URL);
  serverAuthToken_ = env.value("JOURNAL_SERVER_TOKEN").trimmed();
  allowInsecureServerHttp_ =
      environmentFlag(env, "JOURNAL_ALLOW_INSECURE_HTTP");
  allowRemoteSchemaChanges_ =
      environmentFlag(env, "JOURNAL_BOOTSTRAP_REMOTE_SCHEMA");
  QString serverConfigurationError;
  const auto normalizedServerUrl =
      normalizeServerUrl(configuredServerUrl_, &serverConfigurationError);
  if (normalizedServerUrl.has_value())
  {
    configuredServerUrl_ = *normalizedServerUrl;
  }
  else
  {
    qWarning().noquote()
        << "Invalid JOURNAL_SERVER_URL, using loopback default:"
        << serverConfigurationError;
    configuredServerUrl_ = JOURNAL_DEFAULT_SERVER_URL;
  }
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
}

//---------------------------------------------------------------

MainWindow::~MainWindow()
{
  delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (finishMonthDraftBeforeContextChange())
  {
    event->accept();
    return;
  }
  event->ignore();
}

//---------------------------------------------------------------

void MainWindow::setupCalendarControls()
{
  // Встроенная панель навигации дублировала бы компактную панель приложения.
  ui->calendarWidget->setNavigationBarVisible(false);
  ui->previousMonthButton->setMinimumSize(32, 28);
  ui->nextMonthButton->setMinimumSize(32, 28);
  ui->monthSelectorCombo->setMinimumWidth(160);
  ui->toggleCalendarButton->setMinimumHeight(28);

  connect(ui->previousMonthButton, &QToolButton::clicked,
          ui->calendarWidget, &QCalendarWidget::showPreviousMonth);
  connect(ui->nextMonthButton, &QToolButton::clicked, ui->calendarWidget,
          &QCalendarWidget::showNextMonth);

  ui->toggleCalendarButton->setChecked(false);
  applyCalendarExpanded(false);

  connect(ui->toggleCalendarButton, &QToolButton::toggled, this,
          [this](bool isExpanded)
          {
            applyCalendarExpanded(isExpanded);
          });

  updateMonthSelector(ui->calendarWidget->yearShown(),
                      ui->calendarWidget->monthShown());
  connect(ui->monthSelectorCombo,
          qOverload<int>(&QComboBox::activated), this,
          [this](int index)
          {
            const QDate selected =
                ui->monthSelectorCombo->itemData(index).toDate();
            if (selected.isValid())
            {
              ui->calendarWidget->setCurrentPage(selected.year(),
                                                 selected.month());
            }
          });
  connect(ui->calendarWidget, &QCalendarWidget::currentPageChanged, this,
          [this](int shownYear, int shownMonth)
          { handleMonthPageChanged(shownYear, shownMonth); });
}

void MainWindow::updateMonthSelector(int shownYear, int shownMonth)
{
  const QDate firstDay(shownYear, shownMonth, 1);
  if (!firstDay.isValid())
  {
    ui->monthSelectorCombo->clear();
    return;
  }

  std::vector<JournalMonth> months;
  if (journalApp_)
  {
    const auto configured = journalApp_->configuredMonths();
    if (configured.has_value())
    {
      months = *configured;
    }
  }
  if (std::none_of(months.cbegin(), months.cend(),
                   [shownYear, shownMonth](const JournalMonth& value)
                   {
                     return value.year == shownYear &&
                            value.month == shownMonth;
                   }))
  {
    months.push_back({shownYear, shownMonth});
  }
  std::sort(months.begin(), months.end(),
            [](const JournalMonth& lhs, const JournalMonth& rhs)
            {
              return lhs.year != rhs.year ? lhs.year > rhs.year
                                          : lhs.month > rhs.month;
            });

  const QSignalBlocker blocker(ui->monthSelectorCombo);
  ui->monthSelectorCombo->clear();
  const QLocale russian(QLocale::Russian, QLocale::Russia);
  int selectedIndex = -1;
  int previousKey = -1;
  for (const JournalMonth& value : months)
  {
    const QDate date(value.year, value.month, 1);
    const int key = value.year * 100 + value.month;
    if (!date.isValid() || key == previousKey)
    {
      continue;
    }
    previousKey = key;
    QString label = russian.toString(date, "MMMM yyyy");
    if (!label.isEmpty())
    {
      label.replace(0, 1, label.left(1).toUpper());
    }
    if (monthDraft_.has_value() && value.year == monthDraftYear_ &&
        value.month == monthDraftMonth_)
    {
      label += " — черновик";
    }
    ui->monthSelectorCombo->addItem(label, date);
    if (date == firstDay)
    {
      selectedIndex = ui->monthSelectorCombo->count() - 1;
    }
  }
  ui->monthSelectorCombo->setCurrentIndex(selectedIndex);
}

void MainWindow::handleMonthPageChanged(int shownYear, int shownMonth)
{
  if (revertingCalendarPage_)
  {
    return;
  }
  if (hasCurrentMonthDraft() &&
      (shownYear != monthDraftYear_ || shownMonth != monthDraftMonth_) &&
      !finishMonthDraftBeforeContextChange())
  {
    revertingCalendarPage_ = true;
    ui->calendarWidget->setCurrentPage(monthDraftYear_, monthDraftMonth_);
    revertingCalendarPage_ = false;
    updateMonthSelector(monthDraftYear_, monthDraftMonth_);
    return;
  }

  updateMonthSelector(shownYear, shownMonth);
  if (shownYear != dismissedMonthSetupYear_ ||
      shownMonth != dismissedMonthSetupMonth_)
  {
    dismissedMonthSetupYear_ = 0;
    dismissedMonthSetupMonth_ = 0;
  }
  activeDays_.clear();
  createEmptyTable();
  refreshMonth();
}

void MainWindow::applyCalendarExpanded(bool expanded)
{
  ui->calendarWidget->setVisible(expanded);
  ui->toggleCalendarButton->setText(
      expanded ? "Свернуть календарь" : "Развернуть календарь");
  ui->toggleCalendarButton->setToolTip(
      expanded ? "Освободить место для таблицы" : "Показать календарь");
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
  if (hasCurrentMonthDraft())
  {
    monthDataValid_ = true;
    renderMonth(*monthDraft_);
    updateMonthSelector(static_cast<int>(year), static_cast<int>(month));
    updateEditControlsByMode();
    return;
  }
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
  updateMonthSelector(static_cast<int>(year), static_cast<int>(month));
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
      menu.addButton("Добавить из другого месяца", QMessageBox::ActionRole);
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
    addParticipantsFromMonth(true);
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

  // Даты находятся в horizontal header, поэтому остаются видимыми при
  // вертикальной прокрутке списка участников.
  tableWidget->clearContents();
  tableWidget->setRowCount(kFirstContentRow);
  tableWidget->setColumnCount(kFirstDayColumn +
                              static_cast<int>(activeDays_.size()));

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
    tableWidget->setHorizontalHeaderItem(
        column,
        new QTableWidgetItem(dayHeaderLabel(static_cast<int>(year),
                                            static_cast<int>(month), day)));
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
  for (int row = kFirstContentRow; row < tableWidget->rowCount(); ++row)
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
  // Пересоздаем основную таблицу под выбранный месяц.
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
      kFirstContentRow,
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
    tableWidget->setHorizontalHeaderItem(
        i + kFirstDayColumn,
        new QTableWidgetItem(dayHeaderLabel(
            static_cast<int>(year), static_cast<int>(month),
            tableDays.at(i))));
  }

  tableWidget->resizeColumnsToContents();
  auto* header = tableWidget->horizontalHeader();
  header->setDefaultAlignment(Qt::AlignCenter);
  header->setMinimumHeight(tableWidget->fontMetrics().lineSpacing() * 2 + 10);
  header->setSectionResizeMode(QHeaderView::Interactive);
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
            if (row < kFirstContentRow ||
                (column != kNameColumn && column != kRankColumn))
            {
              return;
            }
            const QTableWidgetItem* item = tableWidget->item(row, kNameColumn);
            const ParticipantId id{item ? item->data(Qt::UserRole).toString()
                                        : QString()};
            if (id.isValid())
            {
              if (hasCurrentMonthDraft())
              {
                ui->statusbar->showMessage(
                    "Сначала сохраните или отмените черновик месяца", 4000);
                return;
              }
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
      monthMenu->addAction("Добавить участников из месяца…");
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
          [this]() { addParticipantsFromMonth(); });
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
  if (!journalApp_ ||
      !journalApp_->addUser(static_cast<int>(year), static_cast<int>(month),
                            name))
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
  if (row < kFirstContentRow)
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
  if (!journalApp_ ||
      !journalApp_->removeParticipant(static_cast<int>(year),
                                      static_cast<int>(month), id))
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

bool MainWindow::saveCurrentMonth()
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_ ||
      !monthDataValid_ || activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage("Сохранение запрещено: месяц не загружен");
    return false;
  }
  if (!journalApp_)
  {
    ui->statusbar->showMessage("Сервис не инициализирован");
    return false;
  }

  const auto data = collectMonthFromTable();
  if (hasCurrentMonthDraft())
  {
    MonthSnapshot snapshot = *monthDraft_;
    // Таблица содержит только active days. Скрытые attendance сохраняются,
    // поэтому черновик обновляет записи активных дат, а не заменяет весь list.
    snapshot.attendance.erase(
        std::remove_if(snapshot.attendance.begin(), snapshot.attendance.end(),
                       [this](const AttendanceRecord& record)
                       { return activeDays_.contains(record.day); }),
        snapshot.attendance.end());
    snapshot.attendance.insert(snapshot.attendance.end(), data.begin(),
                               data.end());
    if (!journalApp_->saveMonthSnapshot(monthDraftYear_, monthDraftMonth_,
                                        snapshot))
    {
      ui->statusbar->showMessage("Ошибка сохранения черновика месяца", 6000);
      return false;
    }
    monthDraft_.reset();
    monthDraftYear_ = 0;
    monthDraftMonth_ = 0;
    updateMonthSelector(static_cast<int>(year), static_cast<int>(month));
    updateEditControlsByMode();
    ui->statusbar->showMessage("Черновик месяца сохранён", 5000);
    return true;
  }

  if (!journalApp_->saveAttendance(static_cast<int>(year),
                                   static_cast<int>(month), data))
  {
    ui->statusbar->showMessage("Ошибка сохранения");
    return false;
  }
  ui->statusbar->showMessage("Таблица сохранена");
  return true;
}

bool MainWindow::finishMonthDraftBeforeContextChange()
{
  if (!monthDraft_.has_value())
  {
    return true;
  }

  const QMessageBox::StandardButton decision = QMessageBox::warning(
      this, "Несохранённый месяц",
      QString("Изменения месяца %1.%2 ещё не сохранены.")
          .arg(monthDraftMonth_, 2, 10, QLatin1Char('0'))
          .arg(monthDraftYear_),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Cancel);
  if (decision == QMessageBox::Save)
  {
    return saveCurrentMonth();
  }
  if (decision == QMessageBox::Discard)
  {
    monthDraft_.reset();
    monthDraftYear_ = 0;
    monthDraftMonth_ = 0;
    updateEditControlsByMode();
    return true;
  }
  return false;
}

bool MainWindow::hasCurrentMonthDraft() const
{
  return monthDraft_.has_value() &&
         monthDraftYear_ == static_cast<int>(year) &&
         monthDraftMonth_ == static_cast<int>(month);
}

void MainWindow::updateDraftDayMarker(const ParticipantDayMarker& marker)
{
  if (!hasCurrentMonthDraft())
  {
    return;
  }
  const auto existing = std::find_if(
      monthDraft_->dayMarkers.begin(), monthDraft_->dayMarkers.end(),
      [&marker](const ParticipantDayMarker& value)
      {
        return value.participantId == marker.participantId &&
               value.day == marker.day;
      });
  if (existing == monthDraft_->dayMarkers.end())
  {
    monthDraft_->dayMarkers.push_back(marker);
  }
  else
  {
    *existing = marker;
  }
}

void MainWindow::removeDraftDayMarker(const ParticipantId& participantId,
                                      int day)
{
  if (!hasCurrentMonthDraft())
  {
    return;
  }
  monthDraft_->dayMarkers.erase(
      std::remove_if(monthDraft_->dayMarkers.begin(),
                     monthDraft_->dayMarkers.end(),
                     [&participantId, day](const ParticipantDayMarker& value)
                     {
                       return value.participantId == participantId &&
                              value.day == day;
                     }),
      monthDraft_->dayMarkers.end());
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
  QString error;
  const auto normalized = normalizeServerUrl(
      serverUrl.isEmpty() ? QString(JOURNAL_DEFAULT_SERVER_URL) : serverUrl,
      &error);
  if (!normalized.has_value())
  {
    QMessageBox::warning(this, "Некорректный адрес сервера", error);
  }
  return normalized;
}

std::optional<QString>
MainWindow::normalizeServerUrl(const QString& serverUrl,
                               QString* errorMessage) const
{
  RemoteConnectionOptions options;
  options.baseUrl = serverUrl;
  options.authToken = serverAuthToken_;
  options.timeoutMs = JOURNAL_REMOTE_TIMEOUT_MS;
  options.allowInsecureHttp = allowInsecureServerHttp_;
  options.allowSchemaChanges = allowRemoteSchemaChanges_;
  const auto normalized =
      NormalizeRemoteConnectionOptions(std::move(options), errorMessage);
  return normalized.has_value()
             ? std::optional<QString>(normalized->baseUrl)
             : std::nullopt;
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
  // EventDirectoryDialog модальный: EventApp живёт до завершения exec(). При
  // переходе к modeless окну EventApp должен стать owned dependency диалога.
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

    QString error;
    // Adapter полностью проверяется до замены journalApp_. Неудачный reconnect
    // оставляет прежний storage и загруженный месяц живыми.
    auto remote = createConnectedRemote(targetUrl, &error);
    if (!remote)
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

std::unique_ptr<JournalRemote>
MainWindow::createConnectedRemote(const QString& serverUrl,
                                  QString* errorMessage) const
{
  RemoteConnectionOptions options;
  options.baseUrl = serverUrl;
  options.authToken = serverAuthToken_;
  options.timeoutMs = JOURNAL_REMOTE_TIMEOUT_MS;
  options.allowInsecureHttp = allowInsecureServerHttp_;
  options.allowSchemaChanges = allowRemoteSchemaChanges_;

  auto remote = std::make_unique<JournalRemote>(std::move(options));
  if (!remote->connect(errorMessage))
  {
    return nullptr;
  }
  return remote;
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

void MainWindow::addParticipantsFromMonth(bool copyWeekdayPatternByDefault)
{
  if (isConnectingStorage_ || syncInProgress_ || refreshInProgress_)
  {
    ui->statusbar->showMessage("Дождитесь завершения текущей операции");
    return;
  }
  if (activeStorageMode_ == "server")
  {
    ui->statusbar->showMessage(
        "Добавление участников доступно только в local режиме.", 5000);
    return;
  }

  if (!journalApp_)
  {
    ui->statusbar->showMessage("Сервис не инициализирован");
    return;
  }

  updateCalendarVariables(ui->calendarWidget);
  const auto configuredMonths = journalApp_->configuredMonths();
  if (!configuredMonths.has_value())
  {
    ui->statusbar->showMessage(
        "Не удалось прочитать список сформированных месяцев", 6000);
    return;
  }
  const bool hasSource = std::any_of(
      configuredMonths->cbegin(), configuredMonths->cend(),
      [this](const JournalMonth& value)
      {
        return value.year != static_cast<int>(year) ||
               value.month != static_cast<int>(month);
      });
  if (!hasSource)
  {
    ui->statusbar->showMessage(
        "Нет другого сформированного месяца", 5000);
    return;
  }

  CopyUsersDialog dialog(static_cast<int>(year), static_cast<int>(month),
                         *configuredMonths, this,
                         copyWeekdayPatternByDefault);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }

  const bool applySourceWeekdays = dialog.copyWeekdayPattern();
  const CopyScheduleMode scheduleMode =
      applySourceWeekdays ? CopyScheduleMode::ApplySourceWeekdays
                          : CopyScheduleMode::KeepTargetDates;
  AddParticipantsResult result = journalApp_->prepareParticipantsFromMonth(
      dialog.sourceYear(), dialog.sourceMonth(), static_cast<int>(year),
      static_cast<int>(month), scheduleMode);

  if (!result.ok)
  {
    ui->statusbar->showMessage(
        QString("Добавление не выполнено: %1").arg(result.errorMessage),
        6000);
    return;
  }
  if (result.copied == 0 && !applySourceWeekdays)
  {
    ui->statusbar->showMessage(
        "Все участники этого месяца уже есть в текущем составе", 5000);
    return;
  }

  monthDraft_ = std::move(result.snapshot);
  monthDraftYear_ = static_cast<int>(year);
  monthDraftMonth_ = static_cast<int>(month);
  dismissedMonthSetupYear_ = monthDraftYear_;
  dismissedMonthSetupMonth_ = monthDraftMonth_;
  renderMonth(*monthDraft_);
  updateMonthSelector(monthDraftYear_, monthDraftMonth_);
  updateEditControlsByMode();
  ui->statusbar->showMessage(
      QString("Черновик: добавлено %1, уже были %2. Нажмите «Сохранить "
              "месяц». %3")
          .arg(result.copied)
          .arg(result.skipped)
          .arg(applySourceWeekdays ? "Дни недели источника применены."
                                   : "Даты учёта сохранены без изменений."),
      8000);
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
  const bool draftActive = hasCurrentMonthDraft();
  const bool canChangeStructure = canEditMonth && !draftActive;
  addParticipantAction_->setEnabled(canChangeStructure);
  removeParticipantAction_->setEnabled(canChangeStructure);
  configureMonthAction_->setEnabled(canChangeStructure);
  copyParticipantsAction_->setEnabled(canChangeStructure);
  saveMonthAction_->setEnabled(canEditMonth);
  pushMonthAction_->setEnabled(canEditMonth && !draftActive);
  pullMonthAction_->setEnabled(canEditMonth && !draftActive);
  participantsAction_->setEnabled(controlsIdle && journalApp_ != nullptr &&
                                  !draftActive);
  tournamentsAction_->setEnabled(controlsIdle);
  readLocalAction_->setEnabled(controlsIdle && !draftActive);
  serverUrlAction_->setEnabled(controlsIdle);
  localStorageAction_->setEnabled(controlsIdle && !draftActive);
  remoteStorageAction_->setEnabled(controlsIdle && !draftActive);
  ui->calendarWidget->setEnabled(controlsIdle);
  ui->previousMonthButton->setEnabled(controlsIdle);
  ui->nextMonthButton->setEnabled(controlsIdle);
  ui->monthSelectorCombo->setEnabled(controlsIdle);

  QTableWidget* tableWidget = findChild<QTableWidget*>("bigTable");
  if (tableWidget)
  {
    for (int row = kFirstContentRow; row < tableWidget->rowCount(); ++row)
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
  // Источник таблицы и индикатор режима обязаны совпадать. Иначе следующая
  // команда работает с remote adapter, хотя пользователь видит local данные.
  if (activeStorageMode_ != "local")
  {
    connectLocalStorage();
    return;
  }

  refreshMonth();
  if (monthDataValid_)
  {
    ui->statusbar->showMessage("Локальные данные перечитаны.", 4000);
  }
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
  if (QMessageBox::warning(
          this, "Полная отправка месяца",
          "Серверный месяц будет полностью заменён. Проверки конфликтов "
          "между несколькими клиентами пока нет. Продолжить?",
          QMessageBox::Yes | QMessageBox::Cancel,
          QMessageBox::Cancel) != QMessageBox::Yes)
  {
    return;
  }

  const QString serverUrl = configuredServerUrl_.isEmpty()
                                ? QString(JOURNAL_DEFAULT_SERVER_URL)
                                : configuredServerUrl_;

  QString error;
  updateCalendarVariables(ui->calendarWidget);
  const auto attendance = collectMonthFromTable();
  // Local-first invariant: remote получает только snapshot, уже сохранённый
  // локально. После успешного push обе стороны описывают один aggregate.
  if (!journalApp_->saveAttendance(static_cast<int>(year),
                                   static_cast<int>(month), attendance))
  {
    ui->statusbar->showMessage(
        "Push отменён: не удалось сохранить локальные изменения", 6000);
    return;
  }
  const MonthSnapshot snapshot =
      journalApp_->loadMonth(static_cast<int>(year), static_cast<int>(month));
  if (snapshot.state == MonthState::Error)
  {
    ui->statusbar->showMessage(snapshot.errorMessage, 6000);
    return;
  }
  syncInProgress_ = true;
  updateEditControlsByMode();
  auto remote = createConnectedRemote(serverUrl, &error);
  if (!remote)
  {
    syncInProgress_ = false;
    updateEditControlsByMode();
    ui->statusbar->showMessage(
        QString("Push error: %1")
            .arg(error.isEmpty() ? "не удалось подключиться к серверу"
                                 : error),
        6000);
    return;
  }
  SyncService sync;
  const bool pushed =
      sync.pushMonthToServer(*remote, static_cast<int>(year),
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
  if (QMessageBox::warning(
          this, "Полное получение месяца",
          "Локальный месяц будет полностью заменён серверной копией. "
          "Проверки конфликтов пока нет. Продолжить?",
          QMessageBox::Yes | QMessageBox::Cancel,
          QMessageBox::Cancel) != QMessageBox::Yes)
  {
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
  syncInProgress_ = true;
  updateEditControlsByMode();
  auto remote = createConnectedRemote(serverUrl, &error);
  if (!remote)
  {
    syncInProgress_ = false;
    updateEditControlsByMode();
    ui->statusbar->showMessage(
        QString("Pull error: %1")
            .arg(error.isEmpty() ? "не удалось подключиться к серверу"
                                 : error),
        6000);
    return;
  }
  SyncService sync;
  const bool pulled =
      sync.pullMonthToLocal(*remote, static_cast<int>(year),
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
  QCheckBox* checkBox = cell->attendanceCheckBox();
  const int targetYear = static_cast<int>(year);
  const int targetMonth = static_cast<int>(month);
  connect(cell->attendanceCheckBox(), &QCheckBox::toggled, tableWidget,
          [this, tableWidget, row, checkBox, participantId = participant.id,
           day, targetYear, targetMonth](bool checked)
          {
            updateAttendanceCount(tableWidget, row);
            if (!journalApp_ || activeStorageMode_ != "local" ||
                isConnectingStorage_ || syncInProgress_ ||
                refreshInProgress_ || !monthDataValid_)
            {
              return;
            }
            if (hasCurrentMonthDraft())
            {
              // Черновик собирается из виджетов целиком при явном Save.
              return;
            }

            // Сохраняем только изменённую запись, а не всю таблицу. Это
            // устраняет потерю отметок при смене месяца/закрытии окна и
            // сохраняет стоимость одного клика независимой от размера месяца.
            const std::vector<AttendanceRecord> change = {
                {participantId, day, checked}};
            if (journalApp_->saveAttendance(targetYear, targetMonth, change))
            {
              return;
            }

            // UI не должен показывать значение, которое storage отклонил.
            const QSignalBlocker blocker(checkBox);
            checkBox->setChecked(!checked);
            updateAttendanceCount(tableWidget, row);
            ui->statusbar->showMessage(
                "Не удалось сохранить отметку посещения", 5000);
          });
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
    if (hasCurrentMonthDraft())
    {
      removeDraftDayMarker(participant.id, day);
      cell->setMarker(std::nullopt);
      ui->statusbar->showMessage(
          "Отметка удалена из черновика; сохраните месяц", 4000);
      return;
    }
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
  if (hasCurrentMonthDraft())
  {
    updateDraftDayMarker(marker);
    cell->setMarker(marker);
    ui->statusbar->showMessage(
        "Отметка добавлена в черновик; сохраните месяц", 4000);
    return;
  }
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
  if (!tableWidget || row < kFirstContentRow ||
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
