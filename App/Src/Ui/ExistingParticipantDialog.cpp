#include "ExistingParticipantDialog.hpp"

#include <QCollator>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{

QString participantLabel(const ParticipantProfile& profile)
{
  QString identity = ParticipantDisplayName(profile);
  if (!profile.historicalName.isEmpty() && !profile.fullName.isEmpty() &&
      profile.historicalName != profile.fullName)
  {
    identity = QString("%1 — %2").arg(profile.historicalName,
                                      profile.fullName);
  }
  return QString("%1 · %2")
      .arg(identity, ParticipantRankDisplayName(profile.rank));
}

} // namespace

ExistingParticipantDialog::ExistingParticipantDialog(
    std::vector<ParticipantProfile> profiles, QWidget* parent)
    : QDialog(parent), participantCombo_(new QComboBox(this))
{
  setObjectName("existingParticipantDialog");
  setWindowTitle("Добавление существующего участника");
  participantCombo_->setObjectName("existingParticipantCombo");
  participantCombo_->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  participantCombo_->setMinimumContentsLength(32);
  participantCombo_->setMaxVisibleItems(20);

  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::stable_sort(
      profiles.begin(), profiles.end(),
      [&collator](const ParticipantProfile& lhs,
                  const ParticipantProfile& rhs)
      {
        const int lhsRank = ParticipantRankSortKey(lhs.rank);
        const int rhsRank = ParticipantRankSortKey(rhs.rank);
        if (lhsRank != rhsRank)
        {
          return lhsRank < rhsRank;
        }
        const int byName = collator.compare(ParticipantDisplayName(lhs),
                                            ParticipantDisplayName(rhs));
        return byName != 0 ? byName < 0 : lhs.id.value < rhs.id.value;
      });
  for (const ParticipantProfile& profile : profiles)
  {
    participantCombo_->addItem(participantLabel(profile), profile.id.value);
  }

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel("Участник из общего справочника:", this));
  layout->addWidget(participantCombo_);
  auto* help = new QLabel(
      "Выбранный профиль будет добавлен в состав текущего месяца. "
      "Карточка участника и его история сохранятся без изменений.",
      this);
  help->setWordWrap(true);
  layout->addWidget(help);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName("existingParticipantDialogButtons");
  buttons->button(QDialogButtonBox::Ok)
      ->setEnabled(participantCombo_->count() > 0);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::optional<ParticipantId>
ExistingParticipantDialog::selectedParticipantId() const
{
  ParticipantId id{participantCombo_->currentData().toString()};
  return id.isValid() ? std::optional<ParticipantId>(id) : std::nullopt;
}
