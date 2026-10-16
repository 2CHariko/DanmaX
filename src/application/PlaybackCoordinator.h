#pragma once
#include <QObject>

class PlaybackCoordinator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(Owner owner READ owner NOTIFY changed)
    Q_PROPERTY(quint64 generation READ generation NOTIFY changed)
  public:
    enum Owner { None, Danmaku, Media };
    Q_ENUM(Owner)
    using QObject::QObject;
    Owner owner() const { return owner_; }
    quint64 generation() const { return generation_; }
    void acquire(Owner owner) { owner_ = owner; ++generation_; emit changed(); }
  signals:
    void changed();
  private:
    Owner owner_{None};
    quint64 generation_{};
};
