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

struct PersonProfile {
  QString displayName;
  int age;
  QString profileUrl;
  QString notes;
};

class IJournalStorage {
 public:
  virtual ~IJournalStorage() = default;

  // Интерфейс storage намеренно месяц-ориентированный:
  // UI и use-case работают со снимком месяца, а не с одиночными днями.
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
  // Добавляет пользователя в месяц (как правило, заполняя все дни значением false).
  virtual bool addUser(int year, int month, const QString& name) = 0;
  // Удаляет пользователя из выбранного месяца.
  virtual bool deleteUser(int year, int month, const QString& name) = 0;
  // Читает/сохраняет карточку пользователя.
  virtual bool getPersonProfile(const QString& name, PersonProfile* profile) = 0;
  virtual bool updatePersonProfile(const QString& originalName,
                                   const PersonProfile& profile) = 0;
};
