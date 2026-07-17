#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVector>
#include <QtGlobal>

class QGroupBox;
class SqliteConnect;
class QVBoxLayout;
class AttendanceCellWidget;

#include <memory>
#include <optional>
#include <vector>

#include "JournalApp.hpp"
#include "journal_app.h"

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

  // Элементы панели действий (создаются программно в setupActionPanels()).
  QGroupBox* connectionGroup_;
  QGroupBox* monthGroup_;
  QGroupBox* dataGroup_;
  QLabel* modeBadgeLabel_;
  QLineEdit* serverUrlEdit_;
  QPushButton* connectLocalBtn_;
  QPushButton* connectRemoteBtn_;
  QPushButton* configureMonthBtn_;
  QPushButton* copyUsersBtn_;
  QPushButton* participantsBtn_;

  // Текущее активное подключение (используется для защиты от лишних reconnect).
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

  // Создает вспомогательную таблицу шаблона чекбоксов.
  void createCheckTable();
  // Создает пустую таблицу под выбранный месяц.
  void createEmptyTable();
  // Создает панели действий: подключение, текущий месяц, данные.
  void setupActionPanels();
  void setupConnectionPanel(QVBoxLayout* parentLayout);
  void setupMonthPanel(QVBoxLayout* parentLayout);
  void setupDataPanel(QVBoxLayout* parentLayout);
  // Переключает активный storage на local/server.
  bool setupStorage(const QString& mode, const QString& serverUrl);
  bool openLocalDatabase(SqliteConnect& sqlite);
  // Обработчики кнопок Local/Remote.
  void connectLocalFromUi();
  void connectRemoteFromUi();
  // Открывает диалог выбора дней учета для текущего месяца.
  void configureMonthDays();
  // Переносит пользователей из другого месяца в текущий.
  void copyUsersFromMonth(bool copyWeekdayPatternByDefault = false);
  void openParticipantProfile(const ParticipantId& id);
  void openParticipantDirectory();
  // Обновляет визуальный индикатор режима.
  void updateModeBadge();
  // Включает/выключает кнопки редактирования в зависимости от режима.
  void updateEditControlsByMode();
  // Read Base: всегда читает локальную БД в таблицу.
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
