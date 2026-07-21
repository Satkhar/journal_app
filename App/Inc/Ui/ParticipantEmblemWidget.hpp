#pragma once

#include <QWidget>

#include <optional>

#include "JournalModels.hpp"

class QLabel;
class QPushButton;

// Asset boundary: декодирует недоверенный файл, ограничивает разрешение и
// возвращает канонический PNG без пользовательского пути и EXIF.
std::optional<ParticipantEmblem> NormalizeParticipantEmblemImage(
    const ParticipantId& participantId, const QString& path,
    qint64 expectedRevision, QString* error);

// Редактор держит изменение герба в памяти. БД меняется только вместе с
// карточкой участника после явного нажатия «Сохранить».
class ParticipantEmblemWidget : public QWidget
{
  Q_OBJECT

public:
  ParticipantEmblemWidget(
      const ParticipantId& participantId,
      const std::optional<ParticipantEmblem>& emblem, bool editable,
      QWidget* parent = nullptr);

  ParticipantEmblemAction action() const;
  std::optional<ParticipantEmblem> emblem() const;
  qint64 expectedRevision() const;

signals:
  void changed();

private:
  ParticipantId participantId_;
  std::optional<ParticipantEmblem> original_;
  std::optional<ParticipantEmblem> staged_;
  bool dataAvailable_;
  ParticipantEmblemAction action_;
  QLabel* preview_;
  QLabel* details_;
  QPushButton* chooseButton_;
  QPushButton* removeButton_;

  void chooseImage();
  void removeImage();
  void refreshPreview();
};
