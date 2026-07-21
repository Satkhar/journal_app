#include "ParticipantEmblemWidget.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{

constexpr int kPreviewSize = 180;
constexpr int kMaxSourceDimension = 4096;

} // namespace

std::optional<ParticipantEmblem>
NormalizeParticipantEmblemImage(const ParticipantId& participantId,
                                const QString& path,
                                qint64 expectedRevision, QString* error)
{
  QString ignoredError;
  if (!error)
  {
    error = &ignoredError;
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize sourceSize = reader.size();
  if (!sourceSize.isValid() || sourceSize.width() > kMaxSourceDimension ||
      sourceSize.height() > kMaxSourceDimension)
  {
    *error = "Некорректный или слишком большой размер изображения";
    return std::nullopt;
  }
  QSize targetSize = sourceSize;
  targetSize.scale(kMaxParticipantEmblemDimension,
                   kMaxParticipantEmblemDimension, Qt::KeepAspectRatio);
  reader.setScaledSize(targetSize);
  const QImage image = reader.read();
  if (image.isNull())
  {
    *error = QString("Не удалось декодировать изображение: %1")
                 .arg(reader.errorString());
    return std::nullopt;
  }

  // Новый pixel buffer гарантирует, что EXIF и текстовые metadata исходного
  // файла не попадут в канонический asset вместе с изображением.
  QImage canonical(image.size(), QImage::Format_ARGB32_Premultiplied);
  canonical.fill(Qt::transparent);
  {
    QPainter painter(&canonical);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, image);
  }

  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly) || !canonical.save(&buffer, "PNG"))
  {
    *error = "Не удалось подготовить PNG-копию герба";
    return std::nullopt;
  }
  if (png.size() > kMaxParticipantEmblemBytes)
  {
    *error = "После обработки изображение превышает 5 МиБ";
    return std::nullopt;
  }

  ParticipantEmblem emblem;
  emblem.participantId = participantId;
  emblem.imageData = png;
  emblem.sha256 =
      QCryptographicHash::hash(png, QCryptographicHash::Sha256);
  emblem.originalFileName = QFileInfo(path).fileName();
  emblem.pixelWidth = canonical.width();
  emblem.pixelHeight = canonical.height();
  emblem.revision = expectedRevision;
  if (!emblem.isValid())
  {
    *error = "Подготовленный герб не прошёл проверку";
    return std::nullopt;
  }
  return emblem;
}

ParticipantEmblemWidget::ParticipantEmblemWidget(
    const ParticipantId& participantId,
    const std::optional<ParticipantEmblem>& emblem, bool editable,
    QWidget* parent)
    : QWidget(parent), participantId_(participantId), original_(emblem),
      staged_(emblem), dataAvailable_(editable || emblem.has_value()),
      action_(ParticipantEmblemAction::Keep),
      preview_(new QLabel(this)), details_(new QLabel(this)),
      chooseButton_(new QPushButton(emblem ? "Заменить…" : "Выбрать…", this)),
      removeButton_(new QPushButton("Удалить", this))
{
  setObjectName("participantEmblemWidget");
  preview_->setObjectName("participantEmblemPreview");
  preview_->setFixedSize(kPreviewSize, kPreviewSize);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setStyleSheet(
      "border: 1px solid #B0BEC5; border-radius: 4px; background: #FAFAFA;");
  details_->setObjectName("participantEmblemDetails");
  details_->setWordWrap(true);
  chooseButton_->setObjectName("participantEmblemChooseButton");
  removeButton_->setObjectName("participantEmblemRemoveButton");
  chooseButton_->setEnabled(editable);
  removeButton_->setEnabled(editable && emblem.has_value());

  connect(chooseButton_, &QPushButton::clicked, this,
          [this]() { chooseImage(); });
  connect(removeButton_, &QPushButton::clicked, this,
          [this]() { removeImage(); });

  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(chooseButton_);
  buttonLayout->addWidget(removeButton_);
  buttonLayout->addStretch(1);
  auto* rightLayout = new QVBoxLayout();
  rightLayout->addWidget(details_);
  rightLayout->addLayout(buttonLayout);
  rightLayout->addStretch(1);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(preview_);
  layout->addLayout(rightLayout, 1);
  refreshPreview();
}

ParticipantEmblemAction ParticipantEmblemWidget::action() const
{
  return action_;
}

std::optional<ParticipantEmblem> ParticipantEmblemWidget::emblem() const
{
  return action_ == ParticipantEmblemAction::Replace ? staged_
                                                      : std::nullopt;
}

qint64 ParticipantEmblemWidget::expectedRevision() const
{
  if (action_ == ParticipantEmblemAction::Replace && staged_.has_value())
  {
    return staged_->revision;
  }
  return action_ == ParticipantEmblemAction::Remove && original_.has_value()
             ? original_->revision
             : 0;
}

void ParticipantEmblemWidget::chooseImage()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "Выберите изображение герба", QString(),
      "Изображения (*.png *.jpg *.jpeg *.webp *.bmp);;Все файлы (*)");
  if (path.isEmpty())
  {
    return;
  }
  QString error;
  const qint64 expected = original_.has_value() ? original_->revision : 0;
  const auto loaded = NormalizeParticipantEmblemImage(
      participantId_, path, expected, &error);
  if (!loaded.has_value())
  {
    QMessageBox::warning(this, "Некорректный герб", error);
    return;
  }
  staged_ = *loaded;
  action_ = ParticipantEmblemAction::Replace;
  chooseButton_->setText("Заменить…");
  removeButton_->setEnabled(true);
  refreshPreview();
  emit changed();
}

void ParticipantEmblemWidget::removeImage()
{
  staged_.reset();
  action_ = original_.has_value() ? ParticipantEmblemAction::Remove
                                  : ParticipantEmblemAction::Keep;
  chooseButton_->setText("Выбрать…");
  removeButton_->setEnabled(false);
  refreshPreview();
  emit changed();
}

void ParticipantEmblemWidget::refreshPreview()
{
  if (!staged_.has_value())
  {
    preview_->setPixmap(QPixmap());
    preview_->setText(dataAvailable_ ? "Герб\nне прикреплён"
                                     : "Герб\nнедоступен");
    details_->setText(
        dataAvailable_
            ? "Изображение хранится отдельно от полей профиля."
            : "Гербы пока доступны только в локальном режиме.");
    return;
  }
  QPixmap pixmap;
  if (!pixmap.loadFromData(staged_->imageData, "PNG"))
  {
    preview_->setPixmap(QPixmap());
    preview_->setText("Ошибка\nизображения");
    details_->setText("Сохранённое изображение повреждено.");
    return;
  }
  preview_->setText(QString());
  preview_->setPixmap(pixmap.scaled(preview_->size(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
  details_->setText(QString("%1\n%2 × %3 px")
                        .arg(staged_->originalFileName)
                        .arg(staged_->pixelWidth)
                        .arg(staged_->pixelHeight));
}
