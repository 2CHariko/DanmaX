#include "infrastructure/MediaTitleParser.h"
#include "infrastructure/ChineseConverter.h"
#include <anitomy/anitomy.h>
#include <QRegularExpression>
#include <cmath>

namespace {
QRegularExpression rx(const char* pattern) {
    return QRegularExpression(QString::fromUtf8(pattern), QRegularExpression::CaseInsensitiveOption);
}
QString prepare(QString text) {
    if (rx(R"(^(?:[a-z]:[\\/]|\\\\))").match(text).hasMatch())
        text = text.section(QRegularExpression(QStringLiteral("[/\\\\]")), -1);
    return danmax::toSimplified(text.normalized(QString::NormalizationForm_KC)).trimmed();
}
class Rules final : public IMediaTitleParser {
public:
    QString id() const override { return "rules-v1"; }
    QList<MediaParseCandidate> parse(const MediaParseInput& input) const override {
        auto text = prepare(input.title);
        // Explicit release-group structure, including all-bracket Chinese filenames.
        const auto group = rx(R"(^\[([^\]]*(?:sub|raws|studio|字幕组|制作组)[^\]]*)\]\s*)").match(text);
        if (group.hasMatch()) text.remove(0, group.capturedLength());
        auto result = parseRuleMediaTitle(text);
        return {{result, id(), group.captured(1), {QStringLiteral("中文／显式集数规则")}}};
    }
};
class AnitomyParser final : public IMediaTitleParser {
public:
    QString id() const override { return "anitomy-a538eff"; }
    QList<MediaParseCandidate> parse(const MediaParseInput& input) const override {
        anitomy::Anitomy parser;
        const bool ok = parser.Parse(prepare(input.title).toStdWString());
        const auto& fields = parser.elements();
        const auto get = [&](auto field) { return QString::fromStdWString(fields.get(field)); };
        ParsedMediaTitle content;
        content.anime = get(anitomy::kElementAnimeTitle);
        const auto seasonText = get(anitomy::kElementAnimeSeason);
        bool seasonOk = false;
        const auto season = seasonText.toInt(&seasonOk);
        if (seasonOk && season > 1) content.anime += QStringLiteral(" 第%1季").arg(season);
        content.animeKey = canonicalAnimeTitle(content.anime);
        bool numeric = false;
        const auto number = get(anitomy::kElementEpisodeNumber).toDouble(&numeric);
        if (ok && fields.count(anitomy::kElementEpisodeNumber) == 1 &&
            fields.empty(anitomy::kElementEpisodeNumberAlt) && numeric && std::isfinite(number) && number > 0 && number <= 9999 &&
            (seasonText.isEmpty() || (seasonOk && season > 0))) content.episode = number;
        // Bare numbers remain weak even when the library returns Parse == true.
        const auto text = prepare(input.title);
        content.explicitEpisode = rx(R"((?:\bS\d+E\d+|\bEP(?:ISODE)?\s*\d|\s[-–—]\s*\d|[\[【]\d+(?:v\d+)?[\]】]))").match(text).hasMatch();
        return {{content, id(), get(anitomy::kElementReleaseGroup), {QStringLiteral("发布文件名分词")}}};
    }
};
}
HybridMediaTitleParser::HybridMediaTitleParser() {
    parsers_.push_back(std::make_unique<Rules>());
    parsers_.push_back(std::make_unique<AnitomyParser>());
}
HybridMediaTitleParser::HybridMediaTitleParser(std::vector<std::unique_ptr<IMediaTitleParser>> parsers)
    : parsers_(std::move(parsers)) {}
MediaParseResult HybridMediaTitleParser::parse(const MediaParseInput& input) const {
    MediaParseResult result;
    if (input.title.isEmpty() || input.title.size() > 1024) {
        result.reason = QStringLiteral("标题为空或超过 1024 字符，请手动匹配"); return result;
    }
    const auto text = prepare(input.title);
    for (const auto& parser : parsers_) {
        const auto candidates = parser->parse({text});
        for (auto candidate : candidates) {
            if (result.candidates.size() >= 16) break;
            candidate.parser = parser->id();
            candidate.content.anime = candidate.content.anime.left(512);
            candidate.content.animeKey = canonicalAnimeTitle(candidate.content.anime);
            if (!std::isfinite(candidate.content.episode) || candidate.content.episode <= 0 || candidate.content.episode > 9999)
                candidate.content.episode = 0;
            result.candidates.append(candidate);
            if (!candidate.content.anime.isEmpty()) result.searchTitle = candidate.content.anime;
        }
    }
    const bool special = rx(R"((?:(?:^|[^\p{L}\p{N}])(?:SP|OVA|OAD|OAV|OP|ED|PV|CM|NCOP|NCED|MENUS?|TRAILER|TEASER|SPECIALS?)\s*\d*(?=$|[^\p{L}\p{N}])|特别篇|总集篇|合集|映像特典|菜单))").match(text).hasMatch();
    const bool numericTitle = rx(R"(^\d+\s+[-–—]\s+\d+(?:\.[a-z0-9]+)?$)").match(text).hasMatch();
    const bool range = !numericTitle && rx(R"(\d+(?:\.\d+)?\s*(?:话|話|集)?\s*[-~+&～至]\s*(?:EP(?:ISODE)?\s*|E|第\s*)?\d)").match(text).hasMatch();
    if (special || range || rx(R"(\bS00E\d|第\s*0\s*[话集])").match(text).hasMatch()) {
        result.state = MediaParseResult::State::NeedsConfirmation;
        result.reason = QStringLiteral("特殊片段、零集或范围编号需要手动确认"); return result;
    }
    const bool chineseEpisode = rx(R"(第\s*\d+(?:\.\d+)?\s*[话集])").match(text).hasMatch();
    const bool decimalEpisode = rx(R"((?:EP\s*|\s[-–—]\s*|第\s*)\d+\.\d+)").match(text).hasMatch();
    QString selectedParser;
    for (const auto& candidate : result.candidates) {
        if (!candidate.content.valid()) continue;
        if (!result.selected.valid()) { result.selected = candidate.content; selectedParser = candidate.parser; continue; }
        if (std::abs(result.selected.episode - candidate.content.episode) > 0.0001) {
            if (decimalEpisode && selectedParser == "rules-v1" && candidate.parser == "anitomy-a538eff" &&
                std::floor(result.selected.episode) != result.selected.episode &&
                std::floor(result.selected.episode) == candidate.content.episode) continue;
            result.selected = {}; result.state = MediaParseResult::State::NeedsConfirmation;
            result.reason = QStringLiteral("多个解析器的集数冲突，请手动确认"); return result;
        }
        const auto builtIn = [](const QString& id) { return id == "rules-v1" || id == "anitomy-a538eff"; };
        if (result.selected.animeKey != candidate.content.animeKey &&
            (!builtIn(selectedParser) || !builtIn(candidate.parser))) {
            result.selected = {}; result.state = MediaParseResult::State::NeedsConfirmation;
            result.reason = QStringLiteral("多个解析器的番名冲突，请手动确认"); return result;
        }
        // Preserve explicit Chinese grammar; otherwise use the cleaned release title.
        if (!chineseEpisode && candidate.parser == "anitomy-a538eff") {
            result.selected = candidate.content; selectedParser = candidate.parser;
        }
    }
    if (!result.selected.valid()) {
        result.reason = QStringLiteral("未识别出明确番名和集数，请手动搜索匹配"); return result;
    }
    const auto legacy = parseRuleMediaTitle(input.title);
    if (legacy.valid() && legacy.animeKey != result.selected.animeKey)
        result.selected.legacyAnimeKeys.append(legacy.animeKey);
    result.searchTitle = result.selected.anime;
    result.state = result.selected.explicitEpisode ? MediaParseResult::State::Resolved : MediaParseResult::State::NeedsConfirmation;
    result.reason = result.selected.explicitEpisode ? QString() : QStringLiteral("弱集数格式需要手动确认");
    return result;
}
MediaParseResult identifyMediaTitle(const QString& title) {
    static const HybridMediaTitleParser parser;
    return parser.parse({title});
}
