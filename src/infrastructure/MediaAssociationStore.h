#pragma once
#include "infrastructure/MediaTitle.h"
#include <QJsonObject>
#include <QVariantMap>

class MediaAssociationStore {
  public:
    explicit MediaAssociationStore(const QString& dataDirectory);
    QVariantMap exact(const ParsedMediaTitle& title) const;
    QVariantMap series(const ParsedMediaTitle& title) const;
    QString remember(const ParsedMediaTitle& title, const QVariantMap& metadata);
    QString forget(const ParsedMediaTitle& title);
    QVariantMap raw(const QString& title) const;
    QString rememberRaw(const QString& title, const QVariantMap& metadata, const QString& sourceAnime = {});
    QString forgetRaw(const QString& title);
  private:
    QString save();
    QString path_, loadError_;
    QJsonObject entries_;
};
