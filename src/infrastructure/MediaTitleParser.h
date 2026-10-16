#pragma once
#include "infrastructure/MediaTitle.h"
#include <QList>
#include <memory>
#include <vector>

struct MediaParseInput { QString title; };
struct MediaParseCandidate {
    ParsedMediaTitle content;
    QString parser;
    QString releaseGroup;
    QStringList evidence;
};
struct MediaParseResult {
    enum class State { Resolved, NeedsConfirmation, Unrecognized };
    State state{State::Unrecognized};
    ParsedMediaTitle selected;
    QList<MediaParseCandidate> candidates;
    QString searchTitle, reason;
};
// Local extraction only. Network/AI augmentation must run outside this synchronous port.
class IMediaTitleParser {
public:
    virtual ~IMediaTitleParser() = default;
    virtual QString id() const = 0;
    virtual QList<MediaParseCandidate> parse(const MediaParseInput&) const = 0;
};
class HybridMediaTitleParser {
public:
    HybridMediaTitleParser();
    explicit HybridMediaTitleParser(std::vector<std::unique_ptr<IMediaTitleParser>> parsers);
    MediaParseResult parse(const MediaParseInput&) const;
private:
    std::vector<std::unique_ptr<IMediaTitleParser>> parsers_;
};
MediaParseResult identifyMediaTitle(const QString& title);
