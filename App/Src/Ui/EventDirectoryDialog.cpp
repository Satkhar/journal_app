#include "EventDirectoryDialog.hpp"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

#include "EventDialog.hpp"

EventDirectoryDialog::EventDirectoryDialog(
    EventApp& eventApp, std::vector<ParticipantProfile> profiles,
    QWidget* parent)
    : QDialog(parent), eventApp_(eventApp), profiles_(std::move(profiles)),
      table_(new QTableWidget(this))
{
  setWindowTitle("Турниры");
  resize(900, 560);
  table_->setObjectName("eventsTable");
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels(
      {"Дата", "Название", "Наши участники", "Бои"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::ResizeToContents);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  auto* addButton =
      buttons->addButton("Добавить", QDialogButtonBox::ActionRole);
  auto* editButton =
      buttons->addButton("Открыть", QDialogButtonBox::ActionRole);
  auto* removeButton =
      buttons->addButton("Удалить", QDialogButtonBox::DestructiveRole);
  connect(addButton, &QPushButton::clicked, this,
          [this]() { addEvent(); });
  connect(editButton, &QPushButton::clicked, this,
          [this]() { editEvent(); });
  connect(removeButton, &QPushButton::clicked, this,
          [this]() { removeEvent(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int, int) { editEvent(); });

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(table_);
  layout->addWidget(buttons);
  reload();
}

void EventDirectoryDialog::reload()
{
  const auto events = eventApp_.events();
  if (!events.has_value())
  {
    QMessageBox::warning(this, "Ошибка БД турниров",
                         eventApp_.lastError());
    return;
  }
  table_->setRowCount(static_cast<int>(events->size()));
  for (int row = 0; row < static_cast<int>(events->size()); ++row)
  {
    const EventRecord& event = events->at(row);
    auto* dateItem = new QTableWidgetItem(event.date.toString("dd.MM.yyyy"));
    dateItem->setData(Qt::UserRole, event.id.value);
    dateItem->setData(Qt::UserRole + 1, event.revision);
    table_->setItem(row, 0, dateItem);
    table_->setItem(row, 1, new QTableWidgetItem(event.title));
    QStringList names;
    for (const EventParticipantSnapshot& participant : event.participants)
    {
      const QString displayName = participant.displayNameSnapshot.trimmed();
      const QString fullName = participant.fullNameSnapshot.trimmed();
      names.push_back(fullName.isEmpty() || displayName == fullName
                          ? displayName
                          : QString("%1 — %2").arg(displayName, fullName));
    }
    auto* participantsItem = new QTableWidgetItem(names.join(", "));
    participantsItem->setToolTip(names.join("\n"));
    table_->setItem(row, 2, participantsItem);
    auto* boutsItem =
        new QTableWidgetItem(QString::number(event.bouts.size()));
    boutsItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 3, boutsItem);
  }
}

std::optional<EventDirectoryDialog::EventSelection>
EventDirectoryDialog::selectedEvent() const
{
  const int row = table_->currentRow();
  if (row < 0)
  {
    return std::nullopt;
  }
  const QTableWidgetItem* item = table_->item(row, 0);
  EventSelection selection;
  if (item)
  {
    selection.id = {item->data(Qt::UserRole).toString()};
    selection.revision = item->data(Qt::UserRole + 1).toLongLong();
  }
  return selection.id.isValid() && selection.revision >= 1
             ? std::optional<EventSelection>(selection)
             : std::nullopt;
}

void EventDirectoryDialog::addEvent()
{
  EventRecord event;
  event.id = CreateEventId();
  event.date = QDate::currentDate();
  EventDialog dialog(event, profiles_, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }
  if (!eventApp_.save(dialog.eventRecord()))
  {
    QMessageBox::warning(this, "Ошибка сохранения", eventApp_.lastError());
    return;
  }
  reload();
}

void EventDirectoryDialog::editEvent()
{
  const auto selection = selectedEvent();
  if (!selection.has_value())
  {
    QMessageBox::information(this, "Турниры", "Выберите турнир.");
    return;
  }
  const auto event = eventApp_.event(selection->id);
  if (!event.has_value())
  {
    QMessageBox::warning(this, "Ошибка чтения", eventApp_.lastError());
    return;
  }
  EventDialog dialog(*event, profiles_, this);
  if (dialog.exec() != QDialog::Accepted)
  {
    return;
  }
  if (!eventApp_.save(dialog.eventRecord()))
  {
    QMessageBox::warning(this, "Ошибка сохранения", eventApp_.lastError());
    return;
  }
  reload();
}

void EventDirectoryDialog::removeEvent()
{
  const auto selection = selectedEvent();
  if (!selection.has_value())
  {
    QMessageBox::information(this, "Турниры", "Выберите турнир.");
    return;
  }
  if (QMessageBox::question(
          this, "Удаление турнира",
          "Удалить турнир вместе со всеми строками боёв?") !=
      QMessageBox::Yes)
  {
    return;
  }
  if (!eventApp_.remove(selection->id, selection->revision))
  {
    QMessageBox::warning(this, "Ошибка удаления", eventApp_.lastError());
    return;
  }
  reload();
}
