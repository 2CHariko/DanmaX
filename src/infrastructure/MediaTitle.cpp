#include "infrastructure/MediaTitle.h"
#include "infrastructure/ChineseConverter.h"
#include "infrastructure/MediaTitleParser.h"
#include <QRegularExpression>
#include <cmath>

namespace {
QRegularExpression expression(const char* pattern) {
    return QRegularExpression(QString::fromUtf8(pattern), QRegularExpression::CaseInsensitiveOption);
}
int chineseNumber(QString text) {
    bool ok{}; const int numeric = text.toInt(&ok);
    if (ok) return numeric;
    const QString digits = QStringLiteral("零一二三四五六七八九");
    if (text == QStringLiteral("两")) return 2;
    if (text.size() == 1) return digits.indexOf(text);
    const int ten = text.indexOf(QStringLiteral("十"));
    if (ten >= 0 && text.size() <= 3) {
        const int high = ten == 0 ? 1 : digits.indexOf(text.left(1));
        const int low = ten == text.size() - 1 ? 0 : digits.indexOf(text.right(1));
        if (high > 0 && low >= 0) return high * 10 + low;
    }
    return 0;
}
QString clean(QString text) {
    text = danmax::toSimplified(text.normalized(QString::NormalizationForm_KC)).trimmed();
    static const auto extension = expression(R"(\.(?:mkv|mp4|avi|webm|mov|m4v|ts|flv|wmv)$)");
    text.remove(extension);
    static const auto brackets = expression(R"([\[【(]([^\]】)]+)[\]】)])");
    static const auto technical = expression(R"((?:\b\d{3,4}[pi]\b|\b(?:hevc|avc|x26[45]|h[ .]?26[45]|aac|flac|web[ .-]?dl|b[dr]rip|10bit|8bit|chs|cht|简繁|简体|繁体)\b|^[a-f0-9]{8}$))");
    static const auto group = expression(R"(^(?:[^\]]*(?:raws|subs|vcb-studio|lilith-raws)|ani|动漫国字幕组|桜都字幕组|喵萌奶茶屋|悠哈璃羽字幕社)$)");
    auto matches = brackets.globalMatch(text);
    QList<QPair<qsizetype, qsizetype>> removed;
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (technical.match(match.captured(1)).hasMatch() ||
            (match.capturedStart() == 0 && group.match(match.captured(1)).hasMatch()))
            removed.prepend({match.capturedStart(), match.capturedLength()});
    }
    for (const auto& [start, size] : removed) text.remove(start, size);
    return text.simplified();
}
}

QString canonicalAnimeTitle(const QString& title) {
    if (title.size() > 1024) return {};
    auto text = clean(title).toCaseFolded();
    static const auto season = expression(R"((?:第\s*([零一二三四五六七八九十两\d]+)\s*季|\bseason\s*(\d+)\b|\bs(\d{1,2})\b))");
    auto matches = season.globalMatch(text);
    QList<QPair<QRegularExpressionMatch, int>> replacements;
    while (matches.hasNext()) {
        const auto match = matches.next();
        int number = 0;
        for (int i = 1; i <= 3 && number <= 0; ++i) number = chineseNumber(match.captured(i));
        if (number > 0) replacements.prepend({match, number});
    }
    for (const auto& [match, number] : replacements)
        text.replace(match.capturedStart(), match.capturedLength(), number == 1 ? QStringLiteral(" ") : QStringLiteral(" season%1 ").arg(number));
    static const auto separators = expression(R"([\s\p{P}\p{S}_]+)");
    text.remove(separators);
    return text;
}

QString ParsedMediaTitle::key() const {
    return valid() ? animeKey + QLatin1Char('|') + QString::number(episode, 'g', 12) : QString();
}

ParsedMediaTitle parseRuleMediaTitle(const QString& title) {
    ParsedMediaTitle result;
    if (title.isEmpty() || title.size() > 1024) return result;
    auto text = title;
    static const auto path = expression(R"(^(?:[a-z]:[\\/]|\\\\))");
    if (path.match(text).hasMatch()) text = text.section(QRegularExpression(QStringLiteral("[/\\\\]")), -1);
    text = clean(text);
    // Specials and batches require confirmation; never interpret OP/ED as an episode.
    static const auto special = expression(R"((?:\b(?:SP|OVA|OAD|OP|ED|PV|NCOP|NCED)\s*\d*\b|特别篇|总集篇|合集))");
    if (special.match(text).hasMatch()) return result;
    static const QList<QRegularExpression> formats{
        expression(R"(\bS(\d{1,2})\s*E(\d{1,4}(?:\.\d+)?)(?:v\d+)?\b)"),
        expression(R"(第\s*(\d{1,4}(?:\.\d+)?)\s*[话集])"),
        expression(R"(\bEP(?:ISODE)?\s*(\d{1,4}(?:\.\d+)?)(?:v\d+)?\b)"),
        expression(R"(\s[-–—]\s*(\d{1,4}(?:\.\d+)?)(?:v\d+)?(?=\s|$|\[))"),
        expression(R"([\[【](\d{1,4}(?:\.\d+)?)(?:v\d+)?[\]】])"),
        expression(R"(\s(\d{1,4}(?:\.\d+)?)(?:v\d+)?$)")};
    for (qsizetype index = 0; index < formats.size(); ++index) {
        const auto match = formats[index].match(text);
        if (!match.hasMatch()) continue;
        const auto suffix = text.mid(match.capturedEnd());
        static const auto batch = expression(R"(^\s*[-~+&～至]\s*(?:EP(?:ISODE)?\s*|E|第\s*)?\d)");
        if (batch.match(suffix).hasMatch()) return {};
        auto anime = text.left(match.capturedStart()).trimmed();
        anime.remove(QRegularExpression(QStringLiteral("[\\s_–—-]+$")));
        if (index == formats.size() - 1 && anime.endsWith("season", Qt::CaseInsensitive)) return {};
        if (index == 0 && match.captured(1).toInt() <= 0) return {};
        if (index == 0 && match.captured(1).toInt() > 1) anime += QStringLiteral(" 第%1季").arg(match.captured(1).toInt());
        const double episode = match.captured(index == 0 ? 2 : 1).toDouble();
        if (anime.isEmpty() || !(episode > 0) || episode > 9999 || !std::isfinite(episode)) return {};
        result.anime = anime;
        result.animeKey = canonicalAnimeTitle(anime);
        result.episode = episode;
        result.explicitEpisode = index != formats.size() - 1;
        return result;
    }
    return result;
}

ParsedMediaTitle parseMediaTitle(const QString& title) {
    return identifyMediaTitle(title).selected;
}

double danmakuEpisodeNumber(const QString& title, double structured) {
    static const auto batch = expression(R"(\d+(?:\.\d+)?\s*(?:话|話|集)?\s*[-~+&～至]\s*(?:EP(?:ISODE)?\s*|E|第\s*)?\d)");
    static const auto special = expression(R"((?:\b(?:SP|OVA|OAD|OP|ED|PV|NCOP|NCED)\s*\d*\b|特别篇|总集篇|合集))");
    if (batch.match(title).hasMatch() || special.match(title).hasMatch()) return 0;
    if (std::isfinite(structured) && structured > 0 && structured <= 9999) return structured;
    // Episode labels often omit the anime name. Prefix one for the shared parser.
    const auto parsed = parseRuleMediaTitle(QStringLiteral("Title ") + title);
    if (parsed.valid() && parsed.animeKey == QStringLiteral("title")) return parsed.episode;
    static const auto leading = expression(R"(^\s*(\d{1,4}(?:\.\d+)?)(?:\s|$|[、:：]))");
    return leading.match(title).captured(1).toDouble();
}
