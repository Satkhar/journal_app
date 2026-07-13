#include "ParticipantDirectoryDialog.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ParticipantDirectoryDialog::ParticipantDirectoryDialog(
    const std::vector<ParticipantProfile>& profiles, QWidget* parent)
    : QDialog(parent), table_(new QTableWidget(this))
{
  setWindowTitle("Все участники");
  resize(620, 420);
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels({"Имя", "Статус", "ID"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setRowCount(static_cast<int>(profiles.size()));

  for (int row = 0; row < static_cast<int>(profiles.size()); ++row)
  {
    const ParticipantProfile& profile = profiles.at(row);
    auto* nameItem = new QTableWidgetItem(profile.displayName);
    nameItem->setData(Qt::UserRole, profile.id.value);
    table_->setItem(row, 0, nameItem);
    table_->setItem(
        row, 1, new QTableWidgetItem(profile.archived ? "Архив" : "Активен"));
    table_->setItem(row, 2, new QTableWidgetItem(profile.id.value));
  }
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);

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
