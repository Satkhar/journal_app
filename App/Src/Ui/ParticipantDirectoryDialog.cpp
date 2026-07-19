#include "ParticipantDirectoryDialog.hpp"

#include <QColor>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

ParticipantDirectoryDialog::ParticipantDirectoryDialog(
    const std::vector<ParticipantProfile>& profiles, QWidget* parent)
    : QDialog(parent), table_(new QTableWidget(this))
{
  setWindowTitle("Все участники");
  resize(800, 420);
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels(
      {"Историчное имя", "ФИО", "Звание", "Боевая рука", "Статус"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  std::vector<ParticipantProfile> sortedProfiles = profiles;
  std::stable_sort(
      sortedProfiles.begin(), sortedProfiles.end(),
      [](const ParticipantProfile& lhs, const ParticipantProfile& rhs)
      {
        const int lhsRank = ParticipantRankSortKey(lhs.rank);
        const int rhsRank = ParticipantRankSortKey(rhs.rank);
        return lhsRank != rhsRank
                   ? lhsRank < rhsRank
                   : QString::localeAwareCompare(ParticipantDisplayName(lhs),
                                                 ParticipantDisplayName(rhs)) <
                         0;
      });
  table_->setRowCount(static_cast<int>(sortedProfiles.size()));

  for (int row = 0; row < static_cast<int>(sortedProfiles.size()); ++row)
  {
    const ParticipantProfile& profile = sortedProfiles.at(row);
    auto* nameItem = new QTableWidgetItem(profile.historicalName);
    nameItem->setData(Qt::UserRole, profile.id.value);
    auto* rankItem =
        new QTableWidgetItem(ParticipantRankDisplayName(profile.rank));
    auto* fullNameItem = new QTableWidgetItem(profile.fullName);
    auto* combatHandItem =
        new QTableWidgetItem(CombatHandDisplayName(profile.combatHand));
    const QColor groupColor = ParticipantRankSortKey(profile.rank) % 2 == 0
                                  ? QColor(245, 248, 252)
                                  : QColor(235, 241, 248);
    nameItem->setBackground(groupColor);
    fullNameItem->setBackground(groupColor);
    rankItem->setBackground(groupColor);
    combatHandItem->setBackground(groupColor);
    table_->setItem(row, 0, nameItem);
    table_->setItem(row, 1, fullNameItem);
    table_->setItem(row, 2, rankItem);
    table_->setItem(row, 3, combatHandItem);
    table_->setItem(
        row, 4, new QTableWidgetItem(profile.archived ? "Архив" : "Активен"));
  }
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      4, QHeaderView::ResizeToContents);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto* openButton =
      buttons->addButton("Открыть", QDialogButtonBox::AcceptRole);
  connect(openButton, &QPushButton::clicked, this,
          [this]() { acceptSelection(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int, int) { acceptSelection(); });

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(table_);
  layout->addWidget(buttons);
}

std::optional<ParticipantId> ParticipantDirectoryDialog::selectedId() const
{
  const int row = table_->currentRow();
  if (row < 0)
  {
    return std::nullopt;
  }
  const QTableWidgetItem* item = table_->item(row, 0);
  ParticipantId id{item ? item->data(Qt::UserRole).toString() : QString()};
  return id.isValid() ? std::optional<ParticipantId>(id) : std::nullopt;
}

void ParticipantDirectoryDialog::acceptSelection()
{
  if (selectedId().has_value())
  {
    accept();
  }
}
