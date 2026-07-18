#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCalendarWidget>
#include <QMainWindow>
#include <QStringList>
#include <QTableWidget>
#include <QVector>
#include <QtGlobal>

class QAction;
class QLabel;
class SqliteConnect;
class JournalRemote;
class AttendanceCellWidget;

#include <memory>
#include <optional>
#include <vector>

#include "JournalApp.hpp"

namespace Ui
{
class MainWindow;
}

const QStringList kDaysOfWeek = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вск"};

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  // MainWindow связывает три слоя:
  // UI widgets -> JournalApp use-case -> local/remote storage adapters.
  // Инициализирует UI и связывает кнопки с use-case слоем.
  explicit MainWindow(QWidget* parent = nullptr);
  // Освобождает ресурсы окна.
  ~MainWindow();

private:
  // Основной активный use-case слой, который в данный момент привязан
  // либо к local storage, либо к remote storage (в режиме просмотра).
  Ui::MainWindow* ui;
  std::unique_ptr<JournalApp> journalApp_;

  // Главное окно содержит только календарь и таблицу. Все операции доступны
  // через меню; режим storage виден постоянно в status bar.
  QLabel* modeIndicator_;
  QAction* localStorageAction_;
  QAction* remoteStorageAction_;
  QAction* serverUrlAction_;
  QAction* addParticipantAction_;
  QAction* removeParticipantAction_;
  QAction* configureMonthAction_;
  QAction* copyParticipantsAction_;
  QAction* participantsAction_;
  QAction* readLocalAction_;
  QAction* saveMonthAction_;
  QAction* pushMonthAction_;
  QAction* pullMonthAction_;
  QAction* tournamentsAction_;

  // Текущее активное подключение (используется для защиты от лишних reconnect).
  QString configuredServerUrl_;
  QString serverAuthToken_;
  bool allowInsecureServerHttp_;
  bool allowRemoteSchemaChanges_;
  QString activeStorageMode_;
  QString activeServerUrl_;
  bool isConnectingStorage_;
  bool syncInProgress_;
  bool refreshInProgress_;
  bool monthDataValid_;
  bool monthSetupPromptOpen_;
  quint64 monthSetupRequestId_;
  int dismissedMonthSetupYear_;
  int dismissedMonthSetupMonth_;

  // Кэш указателя на главную таблицу и числовые параметры месяца из календаря.
  QTableWidget* baseTableWidget;
  QVector<int> activeDays_;
  uint32_t day_in_month;
  uint32_t month;
  uint32_t year;

  // Загружает данные текущего месяца и перерисовывает таблицу.
  void refreshMonth();
  void scheduleMonthSetup(const MonthSnapshot& snapshot);
  void showMonthSetupMenu(int targetYear, int targetMonth);
  // Рисует таблицу из снимка месяца.
  void renderMonth(const MonthSnapshot& snapshot);
  // Считывает состояние UI-таблицы в доменную модель.
  std::vector<AttendanceRecord> collectMonthFromTable() const;

  // Создает пустую таблицу под выбранный месяц.
  void createEmptyTable();
  void setupCalendarControls();
  void updateDisplayedMonthLabel(int shownYear, int shownMonth);
  void applyCalendarExpanded(bool expanded);
  // Создает верхнее меню и связывает QAction с use-case обработчиками.
  void setupMenus();
  void addParticipantToMonth();
  void removeSelectedParticipantFromMonth();
  void saveCurrentMonth();
  void configureServerUrl();
  void setConfiguredServerUrl(const QString& serverUrl);
  std::optional<QString> requestServerUrl(const QString& title);
  std::optional<QString>
  normalizeServerUrl(const QString& serverUrl, QString* errorMessage) const;
  // Переключает активный storage на local/server.
  bool setupStorage(const QString& mode, const QString& serverUrl);
  std::unique_ptr<JournalRemote>
  createConnectedRemote(const QString& serverUrl,
                        QString* errorMessage) const;
  bool openLocalDatabase(SqliteConnect& sqlite);
  void connectLocalStorage();
  void connectRemoteStorage();
  // Открывает диалог выбора дней учета для текущего месяца.
  void configureMonthDays();
  // Переносит пользователей из другого месяца в текущий.
  void copyUsersFromMonth(bool copyWeekdayPatternByDefault = false);
  void openParticipantProfile(const ParticipantId& id);
  void openParticipantDirectory();
  void openEventDirectory();
  // Обновляет постоянный индикатор режима и checked-state меню.
  void updateModeIndicator();
  // Включает/выключает действия редактирования в зависимости от режима.
  void updateEditControlsByMode();
  // Read Base: переключает active storage на local и перечитывает месяц.
  // Таблица не должна показывать local snapshot под индикатором REMOTE.
  void readLocalMonthToTable();
  // Push: отправляет текущий месяц из локальной таблицы на сервер.
  void pushCurrentMonthToServer();
  // Pull: читает месяц с сервера и сохраняет в локальную БД.
  void pullCurrentMonthFromServer();

  // Добавляет посещение и семантическую отметку в ячейку дня.
  void addAttendanceCell(
      QTableWidget* tableWidget, int row, int column, bool isChecked,
      const Participant& participant, int day,
      const std::optional<ParticipantDayMarker>& marker);
  void editDayMarker(AttendanceCellWidget* cell,
                     const Participant& participant, int day);
  // Пересчитывает число отмеченных активных дней для строки участника.
  void updateAttendanceCount(QTableWidget* tableWidget, int row);
  // Считывает текущий месяц/год/число дней из календаря.
  void updateCalendarVariables(QCalendarWidget* calendarWidget);
};

#endif // MAINWINDOW_H
