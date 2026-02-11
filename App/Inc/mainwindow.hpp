#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCalendarWidget>
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
  // Инициализирует UI и связывает кнопки с use-case слоем.
  explicit MainWindow(QWidget* parent = nullptr);
  // Освобождает ресурсы окна.
  ~MainWindow();

 private:
  Ui::MainWindow* ui;
  std::unique_ptr<JournalApp> journalApp_;
  QLineEdit* serverUrlEdit_;
  QPushButton* connectLocalBtn_;
  QPushButton* connectRemoteBtn_;
  QString activeStorageMode_;
  QString activeServerUrl_;
  bool isConnectingStorage_;

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
  void setupStorageControls();
  bool setupStorage(const QString& mode, const QString& serverUrl);
  void connectLocalFromUi();
  void connectRemoteFromUi();
  void updateEditControlsByMode();
  void pushCurrentMonthToServer();

  // Ищет колонку по тексту даты в служебной строке.
  int searchDate(QTableWidget* tableWidget, const QString& dateLabel) const;
  // Добавляет чекбокс в указанную ячейку.
  void addCheckBox(QTableWidget* tableWidget, int row, int column, bool is_checked);
  // Считывает текущий месяц/год/число дней из календаря.
  void updateCalendarVariables(QCalendarWidget* calendarWidget);
};

#endif  // MAINWINDOW_H
