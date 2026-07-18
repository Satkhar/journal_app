#include "EventDialog.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{

constexpr int kSideAColumn = 0;
constexpr int kScoreAColumn = 1;
constexpr int kScoreBColumn = 2;
constexpr int kSideBColumn = 3;
constexpr int kRemoveColumn = 4;
constexpr int kMinimumScoreColumnWidth = 96;
constexpr int kMinimumRemoveColumnWidth = 96;
constexpr int kCellWidgetHorizontalMargin = 8;
constexpr char kInternalParticipantIdProperty[] =
    "eventInternalParticipantId";

QString ParticipantChoiceText(const ParticipantProfile& profile)
{
  const QString historicalName = profile.historicalName.trimmed();
  const QString fullName = profile.fullName.trimmed();
  if (!historicalName.isEmpty() && !fullName.isEmpty() &&
      historicalName != fullName)
  {
    return QString("%1 — %2").arg(historicalName, fullName);
  }
  return ParticipantDisplayName(profile);
}

QString ParticipantChoiceText(const EventParticipantSnapshot& participant)
{
  const QString displayName = participant.displayNameSnapshot.trimmed();
  const QString fullName = participant.fullNameSnapshot.trimmed();
  return fullName.isEmpty() || displayName == fullName
             ? displayName
             : QString("%1 — %2").arg(displayName, fullName);
}

} // namespace

EventDialog::EventDialog(const EventRecord& event,
                         const std::vector<ParticipantProfile>& profiles,
                         QWidget* parent)
    : QDialog(parent), original_(event), titleEdit_(new QLineEdit(this)),
      dateEdit_(new QDateEdit(this)), participantsList_(new QListWidget(this)),
      boutsTable_(new QTableWidget(this)), notesEdit_(new QTextEdit(this))
{
  setWindowTitle(event.title.isEmpty() ? "Новый турнир"
                                       : "Редактирование турнира");
  resize(900, 720);

  titleEdit_->setObjectName("eventTitleEdit");
  titleEdit_->setMaxLength(kMaxEventTitleLength);
  titleEdit_->setText(event.title);
  dateEdit_->setObjectName("eventDateEdit");
  dateEdit_->setCalendarPopup(true);
  dateEdit_->setDisplayFormat("dd.MM.yyyy");
  dateEdit_->setDate(event.date.isValid() ? event.date : QDate::currentDate());

  auto* form = new QFormLayout();
  form->addRow("Название", titleEdit_);
  form->addRow("Дата", dateEdit_);

  QHash<QString, EventParticipantSnapshot> choicesById;
  QHash<QString, QString> choiceLabels;
  for (const EventParticipantSnapshot& participant : event.participants)
  {
    choicesById.insert(participant.participantId.value, participant);
    choiceLabels.insert(participant.participantId.value,
                        ParticipantChoiceText(participant));
  }
  for (const ParticipantProfile& profile : profiles)
  {
    if (!profile.id.isValid())
    {
      continue;
    }
    if (!choicesById.contains(profile.id.value))
    {
      choicesById.insert(
          profile.id.value,
          {profile.id, ParticipantDisplayName(profile), profile.fullName});
    }
    QString label = ParticipantChoiceText(profile);
    if (profile.archived)
    {
      label += " (архив)";
    }
    choiceLabels.insert(profile.id.value, label);
  }
  QHash<QString, int> labelCounts;
  for (auto it = choiceLabels.cbegin(); it != choiceLabels.cend(); ++it)
  {
    ++labelCounts[it.value()];
  }
  for (auto it = choiceLabels.begin(); it != choiceLabels.end(); ++it)
  {
    if (labelCounts.value(it.value()) > 1)
    {
      it.value() += QString(" [%1]").arg(it.key().left(8));
    }
  }
  participantChoices_.reserve(choicesById.size());
  for (auto it = choicesById.cbegin(); it != choicesById.cend(); ++it)
  {
    participantChoices_.push_back(it.value());
  }
  std::stable_sort(
      participantChoices_.begin(), participantChoices_.end(),
      [](const EventParticipantSnapshot& lhs,
         const EventParticipantSnapshot& rhs)
      {
        return QString::localeAwareCompare(ParticipantChoiceText(lhs),
                                           ParticipantChoiceText(rhs)) < 0;
      });

  const QSet<QString> selectedIds = [&event]()
  {
    QSet<QString> result;
    for (const EventParticipantSnapshot& participant : event.participants)
    {
      result.insert(participant.participantId.value);
    }
    return result;
  }();
  participantsList_->setObjectName("eventParticipantsList");
  for (const EventParticipantSnapshot& participant : participantChoices_)
  {
    auto* item = new QListWidgetItem(
        choiceLabels.value(participant.participantId.value,
                           participant.displayNameSnapshot),
        participantsList_);
    item->setData(Qt::UserRole, participant.participantId.value);
    item->setData(Qt::UserRole + 1, participant.displayNameSnapshot);
    item->setData(Qt::UserRole + 2, participant.fullNameSnapshot);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(selectedIds.contains(participant.participantId.value)
                            ? Qt::Checked
                            : Qt::Unchecked);
  }
  participantsList_->setMinimumHeight(120);

  boutsTable_->setObjectName("eventBoutsTable");
  boutsTable_->setColumnCount(5);
  boutsTable_->setHorizontalHeaderLabels(
      {"Сторона A", "Счёт A", "Счёт B", "Сторона B", ""});
  boutsTable_->horizontalHeader()->setSectionResizeMode(
      kSideAColumn, QHeaderView::Stretch);
  boutsTable_->horizontalHeader()->setSectionResizeMode(
      kSideBColumn, QHeaderView::Stretch);
  boutsTable_->horizontalHeader()->setSectionResizeMode(
      kScoreAColumn, QHeaderView::Fixed);
  boutsTable_->horizontalHeader()->setSectionResizeMode(
      kScoreBColumn, QHeaderView::Fixed);
  boutsTable_->horizontalHeader()->setSectionResizeMode(
      kRemoveColumn, QHeaderView::Fixed);
  boutsTable_->setColumnWidth(kScoreAColumn, kMinimumScoreColumnWidth);
  boutsTable_->setColumnWidth(kScoreBColumn, kMinimumScoreColumnWidth);
  boutsTable_->setColumnWidth(kRemoveColumn, kMinimumRemoveColumnWidth);
  boutsTable_->verticalHeader()->setVisible(false);
  boutsTable_->verticalHeader()->setMinimumSectionSize(44);
  boutsTable_->verticalHeader()->setDefaultSectionSize(44);
  boutsTable_->setSelectionMode(QAbstractItemView::NoSelection);
  boutsTable_->setMinimumHeight(220);
  for (const EventBout& bout : event.bouts)
  {
    addBoutRow(bout);
  }

  auto* addBoutButton = new QPushButton("Добавить бой", this);
  addBoutButton->setObjectName("addEventBoutButton");
  connect(addBoutButton, &QPushButton::clicked, this,
          [this]() { addBoutRow(); });

  notesEdit_->setObjectName("eventNotesEdit");
  notesEdit_->setAcceptRichText(false);
  notesEdit_->setPlainText(event.notes);
  notesEdit_->setPlaceholderText(
      "Итоги, места, секунданты, ошибки и задачи для разбора");

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText("Сохранить");
  buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
  connect(buttons, &QDialogButtonBox::accepted, this,
          [this]() { save(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(new QLabel("Наши участники", this));
  layout->addWidget(participantsList_);
  auto* boutHint = new QLabel(
      "Выберите нашего участника из списка или введите свободное имя.", this);
  boutHint->setWordWrap(true);
  layout->addWidget(new QLabel("Бои", this));
  layout->addWidget(boutHint);
  layout->addWidget(boutsTable_);
  layout->addWidget(addBoutButton);
  layout->addWidget(new QLabel("Общая информация", this));
  layout->addWidget(notesEdit_);
  layout->addWidget(buttons);
  scheduleBoutTableLayout();
}

QComboBox* EventDialog::createSideCombo(const BoutSideRef& side)
{
  auto* combo = new QComboBox(this);
  combo->setEditable(true);
  combo->setCompleter(nullptr);
  combo->setInsertPolicy(QComboBox::NoInsert);
  combo->lineEdit()->setPlaceholderText("Участник или свободное имя");
  combo->setObjectName("eventBoutSideCombo");
  for (const EventParticipantSnapshot& participant : participantChoices_)
  {
    QString label = participant.displayNameSnapshot;
    for (int row = 0; row < participantsList_->count(); ++row)
    {
      const QListWidgetItem* item = participantsList_->item(row);
      if (item->data(Qt::UserRole).toString() ==
          participant.participantId.value)
      {
        label = item->text();
        break;
      }
    }
    combo->addItem(label, participant.participantId.value);
    combo->setItemData(combo->count() - 1, participant.displayNameSnapshot,
                       Qt::UserRole + 1);
    combo->setItemData(combo->count() - 1, participant.fullNameSnapshot,
                       Qt::UserRole + 2);
  }
  if (side.participantId.has_value())
  {
    const int index = combo->findData(side.participantId->value);
    combo->setCurrentIndex(index);
    combo->setProperty(kInternalParticipantIdProperty,
                       side.participantId->value);
  }
  else
  {
    combo->setCurrentIndex(-1);
    combo->setProperty(kInternalParticipantIdProperty, QString());
    combo->setEditText(side.freeName);
  }
  connect(combo, &QComboBox::currentIndexChanged, this,
          [this, combo](int index)
          {
            combo->setProperty(
                kInternalParticipantIdProperty,
                index >= 0 ? combo->itemData(index).toString() : QString());
            updateParticipantRequirements();
          });
  connect(combo->lineEdit(), &QLineEdit::textEdited, this,
          [this, combo](const QString&)
          {
            combo->setProperty(kInternalParticipantIdProperty, QString());
            updateParticipantRequirements();
          });
  return combo;
}

BoutSideRef EventDialog::sideFromCombo(const QComboBox* combo) const
{
  BoutSideRef result;
  if (!combo)
  {
    return result;
  }
  const ParticipantId selectedId{
      combo->property(kInternalParticipantIdProperty).toString()};
  if (selectedId.isValid())
  {
    result.participantId = selectedId;
    return result;
  }
  result.freeName = combo->currentText().trimmed();
  return result;
}

std::optional<EventParticipantSnapshot>
EventDialog::snapshotFromCombo(const QComboBox* combo) const
{
  const BoutSideRef side = sideFromCombo(combo);
  if (!side.participantId.has_value() || !combo)
  {
    return std::nullopt;
  }
  const int index = combo->findData(side.participantId->value);
  if (index < 0)
  {
    return std::nullopt;
  }
  return EventParticipantSnapshot{
      *side.participantId,
      combo->itemData(index, Qt::UserRole + 1).toString(),
      combo->itemData(index, Qt::UserRole + 2).toString()};
}

void EventDialog::addBoutRow(const std::optional<EventBout>& bout)
{
  const EventBout value = bout.value_or(
      EventBout{CreateBoutId(), BoutSideRef(), BoutSideRef(), 0, 0});
  const int row = boutsTable_->rowCount();
  boutsTable_->insertRow(row);
  auto* sideA = createSideCombo(value.sideA);
  sideA->setProperty("boutId", value.id.value);
  auto* sideB = createSideCombo(value.sideB);
  auto* scoreA = new QSpinBox(this);
  auto* scoreB = new QSpinBox(this);
  scoreA->setRange(0, std::numeric_limits<int>::max());
  scoreB->setRange(0, std::numeric_limits<int>::max());
  scoreA->setValue(value.scoreA);
  scoreB->setValue(value.scoreB);
  scoreA->setObjectName("eventBoutScoreA");
  scoreB->setObjectName("eventBoutScoreB");
  auto* removeButton = new QPushButton("Удалить", this);
  removeButton->setObjectName("removeEventBoutButton");
  connect(removeButton, &QPushButton::clicked, this,
          [this, removeButton]() { removeBoutButtonClicked(removeButton); });
  boutsTable_->setCellWidget(row, kSideAColumn, sideA);
  boutsTable_->setCellWidget(row, kScoreAColumn, scoreA);
  boutsTable_->setCellWidget(row, kScoreBColumn, scoreB);
  boutsTable_->setCellWidget(row, kSideBColumn, sideB);
  boutsTable_->setCellWidget(row, kRemoveColumn, removeButton);
  const int scoreColumnWidth =
      std::max({kMinimumScoreColumnWidth, scoreA->sizeHint().width(),
                scoreB->sizeHint().width()}) +
      kCellWidgetHorizontalMargin;
  const int removeColumnWidth =
      std::max(kMinimumRemoveColumnWidth,
               removeButton->sizeHint().width() +
                   kCellWidgetHorizontalMargin);
  boutsTable_->setColumnWidth(kScoreAColumn, scoreColumnWidth);
  boutsTable_->setColumnWidth(kScoreBColumn, scoreColumnWidth);
  boutsTable_->setColumnWidth(kRemoveColumn, removeColumnWidth);
  boutsTable_->setRowHeight(row, 44);
  scheduleBoutTableLayout();
  updateParticipantRequirements();
}

void EventDialog::scheduleBoutTableLayout()
{
  QTimer::singleShot(
      0, boutsTable_,
      [table = boutsTable_]()
      {
        table->updateGeometry();
        table->horizontalHeader()->updateGeometry();
        table->resizeRowsToContents();
        table->doItemsLayout();
        table->viewport()->update();
      });
}

void EventDialog::removeBoutButtonClicked(QWidget* button)
{
  for (int row = 0; row < boutsTable_->rowCount(); ++row)
  {
    if (boutsTable_->cellWidget(row, kRemoveColumn) == button)
    {
      boutsTable_->removeRow(row);
      updateParticipantRequirements();
      return;
    }
  }
}

void EventDialog::updateParticipantRequirements()
{
  QSet<QString> linkedIds;
  for (int row = 0; row < boutsTable_->rowCount(); ++row)
  {
    for (int column : {kSideAColumn, kSideBColumn})
    {
      const auto* combo = qobject_cast<QComboBox*>(
          boutsTable_->cellWidget(row, column));
      const BoutSideRef side = sideFromCombo(combo);
      if (side.participantId.has_value())
      {
        linkedIds.insert(side.participantId->value);
      }
    }
  }

  for (int row = 0; row < participantsList_->count(); ++row)
  {
    QListWidgetItem* item = participantsList_->item(row);
    const bool linked =
        linkedIds.contains(item->data(Qt::UserRole).toString());
    Qt::ItemFlags flags = item->flags() | Qt::ItemIsUserCheckable;
    if (linked)
    {
      item->setCheckState(Qt::Checked);
      flags &= ~Qt::ItemIsUserCheckable;
      item->setToolTip("Используется в строке боя");
    }
    else
    {
      item->setToolTip(QString());
    }
    item->setFlags(flags);
  }
}

EventRecord EventDialog::eventRecord() const
{
  EventRecord result = original_;
  result.title = titleEdit_->text().trimmed();
  result.date = dateEdit_->date();
  result.notes = notesEdit_->toPlainText();
  result.participants.clear();
  result.bouts.clear();

  QHash<QString, EventParticipantSnapshot> selectedParticipants;
  for (int row = 0; row < participantsList_->count(); ++row)
  {
    const QListWidgetItem* item = participantsList_->item(row);
    if (item->checkState() != Qt::Checked)
    {
      continue;
    }
    EventParticipantSnapshot participant;
    participant.participantId = {item->data(Qt::UserRole).toString()};
    participant.displayNameSnapshot =
        item->data(Qt::UserRole + 1).toString();
    participant.fullNameSnapshot =
        item->data(Qt::UserRole + 2).toString();
    selectedParticipants.insert(participant.participantId.value, participant);
  }

  for (int row = 0; row < boutsTable_->rowCount(); ++row)
  {
    const auto* sideACombo = qobject_cast<QComboBox*>(
        boutsTable_->cellWidget(row, kSideAColumn));
    const auto* sideBCombo = qobject_cast<QComboBox*>(
        boutsTable_->cellWidget(row, kSideBColumn));
    const auto* scoreA = qobject_cast<QSpinBox*>(
        boutsTable_->cellWidget(row, kScoreAColumn));
    const auto* scoreB = qobject_cast<QSpinBox*>(
        boutsTable_->cellWidget(row, kScoreBColumn));
    EventBout bout;
    bout.id = {sideACombo ? sideACombo->property("boutId").toString()
                         : QString()};
    bout.sideA = sideFromCombo(sideACombo);
    bout.sideB = sideFromCombo(sideBCombo);
    bout.scoreA = scoreA ? scoreA->value() : -1;
    bout.scoreB = scoreB ? scoreB->value() : -1;
    result.bouts.push_back(bout);
    for (const auto& participant : {snapshotFromCombo(sideACombo),
                                    snapshotFromCombo(sideBCombo)})
    {
      if (participant.has_value())
      {
        selectedParticipants.insert(participant->participantId.value,
                                    *participant);
      }
    }
  }

  for (const EventParticipantSnapshot& choice : participantChoices_)
  {
    const auto found =
        selectedParticipants.constFind(choice.participantId.value);
    if (found != selectedParticipants.cend())
    {
      result.participants.push_back(found.value());
      selectedParticipants.erase(found);
    }
  }
  for (auto it = selectedParticipants.cbegin();
       it != selectedParticipants.cend(); ++it)
  {
    result.participants.push_back(it.value());
  }
  return result;
}

void EventDialog::save()
{
  const EventRecord candidate = eventRecord();
  if (candidate.notes.size() > kMaxEventNotesLength)
  {
    QMessageBox::warning(this, "Слишком длинный текст",
                         "Общая информация превышает 32768 символов.");
    return;
  }
  if (!candidate.isValid())
  {
    QMessageBox::warning(
        this, "Некорректный турнир",
        "Проверьте название, дату, стороны боёв и неотрицательный счёт. "
        "Нельзя указать одного нашего участника с обеих сторон боя.");
    return;
  }
  accept();
}
