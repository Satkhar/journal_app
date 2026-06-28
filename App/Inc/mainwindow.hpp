#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QTableWidget>
#include <QPushButton>

#include <memory>
#include <vector>

#include "JournalApp.hpp"
#include "journal_app.h"

const QStringList kDaysOfWeek = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вск"};

class MainWindow : public QMainWindow {
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

  // Элементы панели подключения storage (создаются программно в setupStorageControls()).
  QLabel* modeBadgeLabel_;
  QLineEdit* serverUrlEdit_;
  QPushButton* connectLocalBtn_;
  QPushButton* connectRemoteBtn_;

  // Текущее активное подключение (используется для защиты от лишних reconnect).
  QString activeStorageMode_;
  QString activeServerUrl_;
  bool isConnectingStorage_;

  // Кэш указателя на главную таблицу и числовые параметры месяца из календаря.
  QTableWidget* baseTableWidget;
  uint32_t day_in_month;
  uint32_t month;
  uint32_t year;

  // Загружает данные текущего месяца и перерисовывает таблицу.
  void refreshMonth();
  // Рисует таблицу из снимка месяца.
  void renderMonth(const MonthSnapshot& snapshot);
  // Считывает состояние UI-таблицы в доменную модель.
  std::vector<AttendanceRecord> collectMonthFromTable() const;

  // Создает вспомогательную таблицу шаблона чекбоксов.
  void createCheckTable();
  // Создает пустую таблицу под выбранный месяц.
  void createEmptyTable();
  // Создает панель выбора источника данных и URL сервера.
  void setupStorageControls();
  // Переключает активный storage на local/server.
  bool setupStorage(const QString& mode, const QString& serverUrl);
  // Обработчики кнопок Local/Remote.
  void connectLocalFromUi();
  void connectRemoteFromUi();
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

  // Ищет колонку по тексту даты в служебной строке.
  int searchDate(QTableWidget* tableWidget, const QString& dateLabel) const;
  // Добавляет чекбокс в указанную ячейку.
  void addCheckBox(QTableWidget* tableWidget, int row, int column, bool is_checked);
  // Считывает текущий месяц/год/число дней из календаря.
  void updateCalendarVariables(QCalendarWidget* calendarWidget);
};

#endif  // MAINWINDOW_H
