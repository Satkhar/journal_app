#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

struct AttendanceRecord {
  // Имя пользователя (ключ строки в таблице).
  QString userName;
  // День месяца [1..31].
  int day;
  // Отметка посещаемости за день.
  bool isChecked;
};

enum class MonthState {
  Missing,
  Ready,
  Error,
};

struct MonthStateResult {
  MonthState state{MonthState::Error};
  QString errorMessage;
};

class IJournalStorage {
 public:
  virtual ~IJournalStorage() = default;

  // Пустой контейнер допустим; ошибка чтения передается отдельно.
  virtual QString lastError() const = 0;
  // Интерфейс storage намеренно месяц-ориентированный:
  // UI и use-case работают со снимком месяца, а не с одиночными днями.
  // Состояние хранится явно: пустой список пользователей не означает новый месяц.
  virtual MonthStateResult getMonthState(int year, int month) = 0;
  // Возвращает список пользователей, у которых есть записи за месяц.
  virtual QStringList getUsersForMonth(int year, int month) = 0;
  // Возвращает дни месяца, включенные в учет.
  virtual QVector<int> getActiveDays(int year, int month) = 0;
  // Сохраняет дни месяца, включенные в учет.
  virtual bool saveActiveDays(int year, int month, const QVector<int>& days) = 0;
  // Возвращает все отметки пользователей за месяц.
  virtual std::vector<AttendanceRecord> getMonth(int year, int month) = 0;
  // Полностью сохраняет срез месяца.
  virtual bool saveMonth(int year, int month,
                         const std::vector<AttendanceRecord>& data) = 0;
  // Атомарно сохраняет marker, дни учета и полный срез месяца.
  virtual bool saveMonthSetup(
      int year, int month, const QVector<int>& days,
      const std::vector<AttendanceRecord>& data) = 0;
  // Добавляет пользователя в месяц (как правило, заполняя все дни значением false).
  virtual bool addUser(int year, int month, const QString& name) = 0;
  // Удаляет пользователя из выбранного месяца.
  virtual bool deleteUser(int year, int month, const QString& name) = 0;
};
