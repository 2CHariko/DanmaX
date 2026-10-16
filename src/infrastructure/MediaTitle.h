#pragma once
#include <QString>
#include <QStringList>

struct ParsedMediaTitle {
    QString anime;
    QString animeKey;
    double episode{};
    bool explicitEpisode{};
    QStringList legacyAnimeKeys;
    bool valid() const { return !animeKey.isEmpty() && episode > 0; }
    QString key() const;
};

// Adapter-level text parsing; no network, filesystem or playback side effects.
ParsedMediaTitle parseMediaTitle(const QString& title);
ParsedMediaTitle parseRuleMediaTitle(const QString& title);
QString canonicalAnimeTitle(const QString& title);
double danmakuEpisodeNumber(const QString& title, double structured = 0);
