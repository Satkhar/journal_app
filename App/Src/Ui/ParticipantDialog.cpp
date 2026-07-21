#include "ParticipantDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

#include "ParticipantStatisticsWidget.hpp"
#include "ParticipantEmblemWidget.hpp"
#include "ParticipantStrikeHistoryDialog.hpp"
#include "JournalApp.hpp"

ParticipantDialog::ParticipantDialog(const ParticipantProfile& profile,
                                     bool editable, QWidget* parent)
    : ParticipantDialog(profile, ParticipantStatisticsData(), std::nullopt,
                        nullptr, editable, parent)
{
}

ParticipantDialog::ParticipantDialog(
    const ParticipantProfile& profile,
    const ParticipantStatisticsData& statistics,
    bool editable, QWidget* parent)
    : ParticipantDialog(profile, statistics, std::nullopt, nullptr, editable,
                        parent)
{
}

ParticipantDialog::ParticipantDialog(
    const ParticipantProfile& profile,
    const ParticipantStatisticsData& statistics,
    const std::optional<ParticipantEmblem>& emblem, JournalApp* journalApp,
    bool editable, QWidget* parent)
    : QDialog(parent), original_(profile), action_(Action::Cancel),
      nameEdit_(new QLineEdit(profile.historicalName, this)),
      fullNameEdit_(new QLineEdit(profile.fullName, this)),
      contactEdit_(new QLineEdit(profile.contact, this)),
      birthdayCheck_(new QCheckBox("Дата рождения известна", this)),
      daySpin_(new QSpinBox(this)), monthSpin_(new QSpinBox(this)),
      yearSpin_(new QSpinBox(this)),
      trainingStartCheck_(
          new QCheckBox("Начало тренировок известно", this)),
      trainingStartMonthCombo_(new QComboBox(this)),
      trainingStartYearSpin_(new QSpinBox(this)),
      rankCombo_(new QComboBox(this)),
      combatHandCombo_(new QComboBox(this)),
      emblemWidget_(new ParticipantEmblemWidget(profile.id, emblem, editable,
                                                 this)),
      notesEdit_(new QTextEdit(this)),
      saveButton_(nullptr), archiveButton_(nullptr),
      firstRecordedVisit_(statistics.journal.has_value()
                              ? statistics.journal->firstRecordedVisit
                              : std::optional<QDate>()),
      dirty_(false),
      selectedMonth_(std::nullopt), journalApp_(journalApp)
{
  setWindowTitle("Карточка участника");
  setMinimumWidth(620);
  resize(980, 700);

  nameEdit_->setObjectName("participantHistoricalNameEdit");
  nameEdit_->setMaxLength(kMaxParticipantHistoricalNameLength);
  fullNameEdit_->setObjectName("participantFullNameEdit");
  fullNameEdit_->setMaxLength(kMaxParticipantFullNameLength);
  fullNameEdit_->setPlaceholderText("Фамилия Имя Отчество");
  contactEdit_->setObjectName("participantContactEdit");
  contactEdit_->setMaxLength(kMaxParticipantContactLength);
  contactEdit_->setPlaceholderText("VK, Telegram или телефон");

  daySpin_->setRange(1, 31);
  monthSpin_->setRange(1, 12);
  constexpr int kYearNotSpecified = 1899;
  yearSpin_->setObjectName("participantBirthYearSpinBox");
  yearSpin_->setRange(kYearNotSpecified, QDate::currentDate().year());
  yearSpin_->setSpecialValueText("Не указан");
  yearSpin_->setValue(kYearNotSpecified);
  yearSpin_->setMinimumWidth(105);
  trainingStartCheck_->setObjectName(
      "participantTrainingStartCheckBox");
  trainingStartMonthCombo_->setObjectName(
      "participantTrainingStartMonthComboBox");
  const QLocale russian(QLocale::Russian, QLocale::Russia);
  for (int month = 1; month <= 12; ++month)
  {
    trainingStartMonthCombo_->addItem(
        russian.standaloneMonthName(month, QLocale::LongFormat), month);
  }
  trainingStartMonthCombo_->setCurrentIndex(QDate::currentDate().month() - 1);
  trainingStartYearSpin_->setObjectName(
      "participantTrainingStartYearSpinBox");
  const int storedTrainingStartYear =
      profile.trainingStartMonth.has_value()
          ? profile.trainingStartMonth->year
          : QDate::currentDate().year();
  trainingStartYearSpin_->setRange(
      1900, std::max(QDate::currentDate().year(), storedTrainingStartYear));
  trainingStartYearSpin_->setValue(QDate::currentDate().year());
  rankCombo_->setObjectName("participantRankComboBox");
  for (ParticipantRank rank : ParticipantRanksInDisplayOrder())
  {
    rankCombo_->addItem(ParticipantRankDisplayName(rank),
                        static_cast<int>(rank));
  }
  rankCombo_->setCurrentIndex(
      rankCombo_->findData(static_cast<int>(profile.rank)));
  combatHandCombo_->setObjectName("participantCombatHandComboBox");
  for (CombatHand hand :
       {CombatHand::Unknown, CombatHand::Right, CombatHand::Left})
  {
    combatHandCombo_->addItem(CombatHandDisplayName(hand),
                              static_cast<int>(hand));
  }
  combatHandCombo_->setCurrentIndex(
      combatHandCombo_->findData(static_cast<int>(profile.combatHand)));
  notesEdit_->setPlainText(profile.notes);
  notesEdit_->setAcceptRichText(false);

  if (profile.birthday.has_value())
  {
    birthdayCheck_->setChecked(true);
    daySpin_->setValue(profile.birthday->day);
    monthSpin_->setValue(profile.birthday->month);
    yearSpin_->setValue(profile.birthday->year.value_or(kYearNotSpecified));
  }
  if (profile.trainingStartMonth.has_value())
  {
    trainingStartCheck_->setChecked(true);
    trainingStartMonthCombo_->setCurrentIndex(
        trainingStartMonthCombo_->findData(
            profile.trainingStartMonth->month));
    trainingStartYearSpin_->setValue(profile.trainingStartMonth->year);
  }

  auto* birthdayLayout = new QHBoxLayout();
  birthdayLayout->addWidget(new QLabel("День", this));
  birthdayLayout->addWidget(daySpin_);
  birthdayLayout->addWidget(new QLabel("Месяц", this));
  birthdayLayout->addWidget(monthSpin_);
  birthdayLayout->addWidget(new QLabel("Год", this));
  birthdayLayout->addWidget(yearSpin_);

  auto* trainingStartLayout = new QHBoxLayout();
  trainingStartLayout->addWidget(new QLabel("Месяц", this));
  trainingStartLayout->addWidget(trainingStartMonthCombo_);
  trainingStartLayout->addWidget(new QLabel("Год", this));
  trainingStartLayout->addWidget(trainingStartYearSpin_);

  auto* form = new QFormLayout();
  auto* idLabel = new QLabel(profile.id.value, this);
  idLabel->setObjectName("participantIdLabel");
  idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow("ID", idLabel);
  form->addRow("Историчное имя", nameEdit_);
  form->addRow("ФИО", fullNameEdit_);
  form->addRow("Контакт", contactEdit_);
  form->addRow("Звание", rankCombo_);
  form->addRow("Боевая рука", combatHandCombo_);
  form->addRow("Герб", emblemWidget_);
  form->addRow(QString(), birthdayCheck_);
  form->addRow("Дата рождения", birthdayLayout);
  form->addRow(QString(), trainingStartCheck_);
  form->addRow("Начало тренировок", trainingStartLayout);
  form->addRow("Заметка", notesEdit_);
  form->addRow("Статус",
               new QLabel(profile.archived ? "Архив" : "Активен", this));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  if (editable)
  {
    saveButton_ = buttons->addButton("Сохранить", QDialogButtonBox::AcceptRole);
    archiveButton_ =
        buttons->addButton(profile.archived ? "Восстановить" : "Архивировать",
                           QDialogButtonBox::ActionRole);
    connect(saveButton_, &QPushButton::clicked, this, [this]() { save(); });
    connect(archiveButton_, &QPushButton::clicked, this,
            [this]()
            {
              if (dirty_)
              {
                QMessageBox::warning(
                    this, "Несохраненные изменения",
                    "Сначала сохраните изменения профиля, затем повторно "
                    "откройте карточку для смены статуса.");
                return;
              }
              action_ = Action::ToggleArchive;
              accept();
            });
  }
  else
  {
    nameEdit_->setReadOnly(true);
    fullNameEdit_->setReadOnly(true);
    contactEdit_->setReadOnly(true);
    rankCombo_->setEnabled(false);
    combatHandCombo_->setEnabled(false);
    birthdayCheck_->setEnabled(false);
    trainingStartCheck_->setEnabled(false);
    notesEdit_->setReadOnly(true);
  }
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(nameEdit_, &QLineEdit::textChanged, this,
          [this]() { dirty_ = true; });
  connect(fullNameEdit_, &QLineEdit::textChanged, this,
          [this]() { dirty_ = true; });
  connect(contactEdit_, &QLineEdit::textChanged, this,
          [this]() { dirty_ = true; });
  connect(birthdayCheck_, &QCheckBox::toggled, this,
          [this]()
          {
            dirty_ = true;
            updateBirthdayControls();
          });
  connect(daySpin_, &QSpinBox::valueChanged, this, [this]() { dirty_ = true; });
  connect(monthSpin_, &QSpinBox::valueChanged, this,
          [this]() { dirty_ = true; });
  connect(yearSpin_, &QSpinBox::valueChanged, this,
          [this]() { dirty_ = true; });
  connect(trainingStartCheck_, &QCheckBox::toggled, this,
          [this]()
          {
            dirty_ = true;
            updateTrainingStartControls();
          });
  connect(trainingStartMonthCombo_, &QComboBox::currentIndexChanged, this,
          [this]() { dirty_ = true; });
  connect(trainingStartYearSpin_, &QSpinBox::valueChanged, this,
          [this]() { dirty_ = true; });
  connect(rankCombo_, &QComboBox::currentIndexChanged, this,
          [this]() { dirty_ = true; });
  connect(combatHandCombo_, &QComboBox::currentIndexChanged, this,
          [this]() { dirty_ = true; });
  connect(emblemWidget_, &ParticipantEmblemWidget::changed, this,
          [this]() { dirty_ = true; });
  connect(notesEdit_, &QTextEdit::textChanged, this,
          [this]() { dirty_ = true; });

  auto* profilePage = new QWidget(this);
  profilePage->setObjectName("participantProfilePage");
  auto* profileLayout = new QVBoxLayout(profilePage);
  profileLayout->addLayout(form);

  auto* statisticsWidget = new ParticipantStatisticsWidget(
      statistics, profile.trainingStartMonth, this);
  if (!journalApp_)
  {
    auto* strikeHistoryButton = statisticsWidget->findChild<QPushButton*>(
        "participantStrikeHistoryButton");
    if (strikeHistoryButton)
    {
      strikeHistoryButton->setEnabled(false);
      strikeHistoryButton->setToolTip(
          "История замеров пока доступна только в локальном режиме");
    }
  }
  const auto updateStatisticsTrainingStart =
      [this, statisticsWidget]()
      {
        std::optional<JournalMonth> value;
        if (trainingStartCheck_->isChecked())
        {
          value = JournalMonth{
              trainingStartYearSpin_->value(),
              trainingStartMonthCombo_->currentData().toInt()};
        }
        statisticsWidget->setTrainingStartMonth(value);
      };
  connect(trainingStartCheck_, &QCheckBox::toggled, statisticsWidget,
          updateStatisticsTrainingStart);
  connect(trainingStartMonthCombo_, &QComboBox::currentIndexChanged,
          statisticsWidget, updateStatisticsTrainingStart);
  connect(trainingStartYearSpin_, &QSpinBox::valueChanged, statisticsWidget,
          updateStatisticsTrainingStart);
  connect(statisticsWidget, &ParticipantStatisticsWidget::monthActivated,
          this,
          [this](int year, int month) { openMonth(year, month); });
  connect(statisticsWidget,
          &ParticipantStatisticsWidget::strikeHistoryRequested, this,
          [this, editable]()
          {
            if (!journalApp_)
            {
              QMessageBox::information(
                  this, "Статистика недоступна",
                  "Замеры ударов пока доступны только в локальном режиме.");
              return;
            }
            ParticipantStrikeHistoryDialog dialog(*journalApp_, original_,
                                                   editable, this);
            dialog.exec();
          });

  auto* tabs = new QTabWidget(this);
  tabs->setObjectName("participantTabWidget");
  tabs->addTab(profilePage, "Профиль");
  tabs->addTab(statisticsWidget, "Статистика");

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  layout->addWidget(buttons);
  updateBirthdayControls();
  updateTrainingStartControls();
}

ParticipantDialog::Action ParticipantDialog::action() const
{
  return action_;
}

ParticipantProfile ParticipantDialog::profile() const
{
  ParticipantProfile result = original_;
  result.historicalName = nameEdit_->text().trimmed();
  result.fullName = fullNameEdit_->text().trimmed();
  result.displayName = ParticipantDisplayName(result);
  result.contact = contactEdit_->text().trimmed();
  result.rank = static_cast<ParticipantRank>(rankCombo_->currentData().toInt());
  result.combatHand =
      static_cast<CombatHand>(combatHandCombo_->currentData().toInt());
  result.notes = notesEdit_->toPlainText();
  result.birthday = std::nullopt;
  if (birthdayCheck_->isChecked())
  {
    Birthday birthday{daySpin_->value(), monthSpin_->value(), std::nullopt};
    if (yearSpin_->value() >= 1900)
    {
      birthday.year = yearSpin_->value();
    }
    result.birthday = birthday;
  }
  result.trainingStartMonth = std::nullopt;
  if (trainingStartCheck_->isChecked())
  {
    result.trainingStartMonth =
        JournalMonth{trainingStartYearSpin_->value(),
                     trainingStartMonthCombo_->currentData().toInt()};
  }
  return result;
}

ParticipantCardUpdate ParticipantDialog::cardUpdate() const
{
  ParticipantCardUpdate update;
  update.profile = profile();
  update.emblemAction = emblemWidget_->action();
  update.emblem = emblemWidget_->emblem();
  update.expectedEmblemRevision = emblemWidget_->expectedRevision();
  return update;
}

bool ParticipantDialog::targetArchived() const
{
  return !original_.archived;
}

std::optional<JournalMonth> ParticipantDialog::selectedMonth() const
{
  return selectedMonth_;
}

void ParticipantDialog::updateBirthdayControls()
{
  const bool enabled =
      birthdayCheck_->isChecked() && birthdayCheck_->isEnabled();
  daySpin_->setEnabled(enabled);
  monthSpin_->setEnabled(enabled);
  yearSpin_->setEnabled(enabled);
}

void ParticipantDialog::updateTrainingStartControls()
{
  const bool enabled =
      trainingStartCheck_->isChecked() && trainingStartCheck_->isEnabled();
  trainingStartMonthCombo_->setEnabled(enabled);
  trainingStartYearSpin_->setEnabled(enabled);
}

void ParticipantDialog::save()
{
  const ParticipantProfile edited = profile();
  if (edited.trainingStartMonth.has_value() &&
      !IsTrainingStartMonthNotAfter(edited.trainingStartMonth,
                                    QDate::currentDate()))
  {
    QMessageBox::warning(
        this, "Некорректное начало тренировок",
        "Месяц начала тренировок не может быть в будущем.");
    return;
  }
  if (edited.trainingStartMonth.has_value() &&
      firstRecordedVisit_.has_value())
  {
    const QDate trainingStart(edited.trainingStartMonth->year,
                              edited.trainingStartMonth->month, 1);
    const QDate firstVisitMonth(firstRecordedVisit_->year(),
                                firstRecordedVisit_->month(), 1);
    if (trainingStart > firstVisitMonth)
    {
      // Это предупреждение, а не storage-инвариант: profile и исторические
      // month snapshots могут импортироваться или синхронизироваться отдельно.
      const QMessageBox::StandardButton choice = QMessageBox::warning(
          this, "Проверьте начало тренировок",
          QString("Указанное начало тренировок позже первого посещения "
                  "в журнале (%1). Сохранить это ручное значение?")
              .arg(firstRecordedVisit_->toString("dd.MM.yyyy")),
          QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
      if (choice != QMessageBox::Save)
      {
        return;
      }
    }
  }
  if ((edited.historicalName.isEmpty() && edited.fullName.isEmpty()) ||
      !edited.isValid())
  {
    QMessageBox::warning(this, "Некорректный профиль",
                         "Укажите ФИО или историчное имя. Проверьте контакт, "
                         "дату рождения, месяц начала тренировок и длину "
                         "заметки (не более 4096 символов).");
    return;
  }
  action_ = Action::Save;
  accept();
}

void ParticipantDialog::openMonth(int year, int month)
{
  if (!QDate(year, month, 1).isValid())
  {
    return;
  }
  if (dirty_)
  {
    QMessageBox::warning(
        this, "Несохраненные изменения",
        "Сохраните или отмените изменения профиля перед переходом к месяцу.");
    return;
  }
  selectedMonth_ = JournalMonth{year, month};
  action_ = Action::OpenMonth;
  accept();
}
