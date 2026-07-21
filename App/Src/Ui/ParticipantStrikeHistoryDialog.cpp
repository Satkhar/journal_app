#include "ParticipantStrikeHistoryDialog.hpp"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

#include "JournalApp.hpp"

namespace
{

QString Rate(const TimedStrikeTest& test)
{
  return QLocale().toString(test.strikesPerSecond(), 'f', 2);
}

bool SameConditions(const TimedStrikeTest& lhs, const TimedStrikeTest& rhs)
{
  return lhs.hand == rhs.hand && lhs.weapon == rhs.weapon &&
         lhs.durationSeconds == rhs.durationSeconds;
}

} // namespace

TimedStrikeTestDialog::TimedStrikeTestDialog(const TimedStrikeTest& test,
                                             QWidget* parent)
    : QDialog(parent), original_(test),
      performedAtEdit_(new QDateTimeEdit(this)), handCombo_(new QComboBox(this)),
      strikeCountSpin_(new QSpinBox(this)), durationSpin_(new QSpinBox(this)),
      weaponCombo_(new QComboBox(this)), noteEdit_(new QTextEdit(this))
{
  setWindowTitle(test.revision == 0 ? "Новый замер ударов"
                                    : "Редактирование замера");
  setMinimumWidth(460);
  performedAtEdit_->setObjectName("strikeTestPerformedAtEdit");
  performedAtEdit_->setCalendarPopup(true);
  performedAtEdit_->setDisplayFormat("dd.MM.yyyy HH:mm:ss");
  performedAtEdit_->setDateTime(test.performedAt.toLocalTime());
  handCombo_->setObjectName("strikeTestHandCombo");
  for (StrikeHand hand : {StrikeHand::Right, StrikeHand::Left})
  {
    handCombo_->addItem(StrikeHandDisplayName(hand), static_cast<int>(hand));
  }
  handCombo_->setCurrentIndex(
      handCombo_->findData(static_cast<int>(test.hand)));
  strikeCountSpin_->setObjectName("strikeTestCountSpin");
  strikeCountSpin_->setRange(1, 100000);
  strikeCountSpin_->setValue(std::max(1, test.strikeCount));
  durationSpin_->setObjectName("strikeTestDurationSpin");
  durationSpin_->setRange(1, 3600);
  durationSpin_->setSuffix(" с");
  durationSpin_->setValue(std::max(1, test.durationSeconds));
  weaponCombo_->setObjectName("strikeTestWeaponCombo");
  for (StrikeWeapon weapon : {StrikeWeapon::Sword, StrikeWeapon::Tyambara})
  {
    weaponCombo_->addItem(StrikeWeaponDisplayName(weapon),
                          static_cast<int>(weapon));
  }
  weaponCombo_->setCurrentIndex(
      weaponCombo_->findData(static_cast<int>(test.weapon)));
  noteEdit_->setObjectName("strikeTestNoteEdit");
  noteEdit_->setAcceptRichText(false);
  noteEdit_->setPlainText(test.note);

  auto* form = new QFormLayout();
  form->addRow("Дата и время", performedAtEdit_);
  form->addRow("Рука", handCombo_);
  form->addRow("Количество ударов", strikeCountSpin_);
  form->addRow("Длительность", durationSpin_);
  form->addRow("Оружие", weaponCombo_);
  form->addRow("Примечание", noteEdit_);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this,
          [this]() { acceptIfValid(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

TimedStrikeTest TimedStrikeTestDialog::test() const
{
  TimedStrikeTest result = original_;
  result.performedAt = performedAtEdit_->dateTime().toUTC();
  result.hand = static_cast<StrikeHand>(handCombo_->currentData().toInt());
  result.strikeCount = strikeCountSpin_->value();
  result.durationSeconds = durationSpin_->value();
  result.weapon =
      static_cast<StrikeWeapon>(weaponCombo_->currentData().toInt());
  result.note = noteEdit_->toPlainText();
  return result;
}

void TimedStrikeTestDialog::acceptIfValid()
{
  if (!test().isValid())
  {
    QMessageBox::warning(
        this, "Некорректный замер",
        "Проверьте дату, руку, количество, длительность, оружие и длину "
        "примечания (не более 4096 символов).");
    return;
  }
  accept();
}

ParticipantStrikeHistoryDialog::ParticipantStrikeHistoryDialog(
    JournalApp& app, const ParticipantProfile& profile, bool editable,
    QWidget* parent)
    : QDialog(parent), app_(app), profile_(profile), editable_(editable),
      summary_(new QLabel(this)), table_(new QTableWidget(this)),
      addButton_(new QPushButton("Добавить…", this)),
      editButton_(new QPushButton("Изменить…", this)),
      removeButton_(new QPushButton("Удалить", this))
{
  setWindowTitle(QString("Удары на время — %1").arg(profile.displayName));
  resize(980, 520);
  summary_->setObjectName("strikeHistorySummary");
  summary_->setWordWrap(true);
  table_->setObjectName("strikeHistoryTable");
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels(
      {"Дата", "Рука", "Удары", "Секунды", "Оружие", "Уд/с", "Уд/мин",
       "Примечание"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(0,
                                                   QHeaderView::ResizeToContents);
  for (int column = 1; column <= 6; ++column)
  {
    table_->horizontalHeader()->setSectionResizeMode(
        column, QHeaderView::ResizeToContents);
  }
  table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
  addButton_->setObjectName("addStrikeTestButton");
  editButton_->setObjectName("editStrikeTestButton");
  removeButton_->setObjectName("removeStrikeTestButton");
  addButton_->setEnabled(editable_);
  editButton_->setEnabled(editable_);
  removeButton_->setEnabled(editable_);
  connect(addButton_, &QPushButton::clicked, this, [this]() { addTest(); });
  connect(editButton_, &QPushButton::clicked, this, [this]() { editTest(); });
  connect(removeButton_, &QPushButton::clicked, this,
          [this]() { removeTest(); });
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int, int)
          {
            if (editable_)
            {
              editTest();
            }
          });
  auto* actionLayout = new QHBoxLayout();
  actionLayout->addWidget(addButton_);
  actionLayout->addWidget(editButton_);
  actionLayout->addWidget(removeButton_);
  actionLayout->addStretch(1);
  auto* closeButtons =
      new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(summary_);
  layout->addWidget(table_, 1);
  layout->addLayout(actionLayout);
  layout->addWidget(closeButtons);
  reload();
}

void ParticipantStrikeHistoryDialog::reload()
{
  const auto loaded = app_.timedStrikeTests(profile_.id);
  if (!loaded.has_value())
  {
    tests_.clear();
    table_->setRowCount(0);
    summary_->setText(QString("Статистика недоступна: %1")
                          .arg(app_.lastError()));
    return;
  }
  tests_ = *loaded;
  std::sort(tests_.begin(), tests_.end(),
            [](const TimedStrikeTest& lhs, const TimedStrikeTest& rhs)
            {
              return lhs.performedAt != rhs.performedAt
                         ? lhs.performedAt > rhs.performedAt
                         : lhs.id.value > rhs.id.value;
            });
  table_->setRowCount(static_cast<int>(tests_.size()));
  const QLocale locale;
  for (int row = 0; row < static_cast<int>(tests_.size()); ++row)
  {
    const TimedStrikeTest& test = tests_.at(row);
    auto* dateItem = new QTableWidgetItem(
        test.performedAt.toLocalTime().toString("dd.MM.yyyy HH:mm:ss"));
    dateItem->setData(Qt::UserRole, test.id.value);
    table_->setItem(row, 0, dateItem);
    table_->setItem(row, 1,
                    new QTableWidgetItem(StrikeHandDisplayName(test.hand)));
    table_->setItem(row, 2,
                    new QTableWidgetItem(QString::number(test.strikeCount)));
    table_->setItem(
        row, 3, new QTableWidgetItem(QString::number(test.durationSeconds)));
    table_->setItem(
        row, 4, new QTableWidgetItem(StrikeWeaponDisplayName(test.weapon)));
    table_->setItem(row, 5, new QTableWidgetItem(Rate(test)));
    table_->setItem(
        row, 6,
        new QTableWidgetItem(locale.toString(test.strikesPerMinute(), 'f', 1)));
    table_->setItem(row, 7, new QTableWidgetItem(test.note));
  }
  updateSummary();
}

void ParticipantStrikeHistoryDialog::updateSummary()
{
  if (tests_.empty())
  {
    summary_->setText(
        "Замеров пока нет. Для прогресса сравниваются только одинаковые "
        "рука, оружие и длительность.");
    return;
  }
  const TimedStrikeTest& latest = tests_.front();
  std::vector<TimedStrikeTest> comparable;
  std::copy_if(tests_.begin(), tests_.end(), std::back_inserter(comparable),
               [&latest](const TimedStrikeTest& value)
               { return SameConditions(value, latest); });
  const auto chronological = [](const TimedStrikeTest& lhs,
                                const TimedStrikeTest& rhs)
  { return lhs.performedAt < rhs.performedAt; };
  std::sort(comparable.begin(), comparable.end(), chronological);
  const TimedStrikeTest& first = comparable.front();
  const auto best = std::max_element(
      comparable.begin(), comparable.end(),
      [](const TimedStrikeTest& lhs, const TimedStrikeTest& rhs)
      { return lhs.strikesPerSecond() < rhs.strikesPerSecond(); });
  const double delta = latest.strikesPerSecond() - first.strikesPerSecond();
  summary_->setText(
      QString("Последние условия: %1, %2, %3 с. Последний результат: %4 "
              "уд/с; лучший: %5 уд/с; изменение от первого: %6%7 уд/с.")
          .arg(StrikeWeaponDisplayName(latest.weapon),
               StrikeHandDisplayName(latest.hand))
          .arg(latest.durationSeconds)
          .arg(Rate(latest))
          .arg(Rate(*best))
          .arg(delta >= 0.0 ? "+" : QString())
          .arg(QLocale().toString(delta, 'f', 2)));
}

std::optional<TimedStrikeTest>
ParticipantStrikeHistoryDialog::selectedTest() const
{
  const int row = table_->currentRow();
  if (row < 0 || row >= table_->rowCount() || !table_->item(row, 0))
  {
    return std::nullopt;
  }
  const QString id = table_->item(row, 0)->data(Qt::UserRole).toString();
  const auto found = std::find_if(
      tests_.begin(), tests_.end(), [&id](const TimedStrikeTest& value)
      { return value.id.value == id; });
  return found == tests_.end() ? std::nullopt
                               : std::optional<TimedStrikeTest>(*found);
}

void ParticipantStrikeHistoryDialog::addTest()
{
  TimedStrikeTest value;
  value.id = CreateTimedStrikeTestId();
  value.participantId = profile_.id;
  value.performedAt = QDateTime::currentDateTimeUtc();
  value.hand = profile_.combatHand == CombatHand::Left ? StrikeHand::Left
                                                       : StrikeHand::Right;
  value.strikeCount = 1;
  value.durationSeconds = 30;
  TimedStrikeTestDialog dialog(value, this);
  if (dialog.exec() == QDialog::Accepted &&
      !app_.saveTimedStrikeTest(dialog.test()))
  {
    QMessageBox::warning(this, "Ошибка сохранения", app_.lastError());
  }
  reload();
}

void ParticipantStrikeHistoryDialog::editTest()
{
  const auto value = selectedTest();
  if (!value.has_value())
  {
    QMessageBox::information(this, "Замер не выбран",
                             "Выберите строку замера.");
    return;
  }
  TimedStrikeTestDialog dialog(*value, this);
  if (dialog.exec() == QDialog::Accepted &&
      !app_.saveTimedStrikeTest(dialog.test()))
  {
    QMessageBox::warning(
        this, "Ошибка сохранения",
        QString("Запись могла быть изменена в другом окне. %1")
            .arg(app_.lastError()));
  }
  reload();
}

void ParticipantStrikeHistoryDialog::removeTest()
{
  const auto value = selectedTest();
  if (!value.has_value())
  {
    QMessageBox::information(this, "Замер не выбран",
                             "Выберите строку замера.");
    return;
  }
  if (QMessageBox::question(this, "Удаление замера",
                            "Удалить выбранный замер?") != QMessageBox::Yes)
  {
    return;
  }
  if (!app_.removeTimedStrikeTest(value->id, value->revision))
  {
    QMessageBox::warning(this, "Ошибка удаления", app_.lastError());
  }
  reload();
}
