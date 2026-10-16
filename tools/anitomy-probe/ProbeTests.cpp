#include "infrastructure/MediaTitle.h"
#include "infrastructure/MediaTitleParser.h"
#include <anitomy/anitomy.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTest>
#include <cmath>

namespace {
struct Field { const char* name; anitomy::ElementCategory category; };
constexpr Field fields[]{
    {"anime_season", anitomy::kElementAnimeSeason}, {"anime_title", anitomy::kElementAnimeTitle},
    {"anime_type", anitomy::kElementAnimeType}, {"anime_year", anitomy::kElementAnimeYear},
    {"audio_term", anitomy::kElementAudioTerm}, {"episode_number", anitomy::kElementEpisodeNumber},
    {"episode_number_alt", anitomy::kElementEpisodeNumberAlt}, {"episode_title", anitomy::kElementEpisodeTitle},
    {"file_checksum", anitomy::kElementFileChecksum}, {"file_extension", anitomy::kElementFileExtension},
    {"file_name", anitomy::kElementFileName}, {"language", anitomy::kElementLanguage},
    {"other", anitomy::kElementOther}, {"release_group", anitomy::kElementReleaseGroup},
    {"release_information", anitomy::kElementReleaseInformation}, {"release_version", anitomy::kElementReleaseVersion},
    {"source", anitomy::kElementSource}, {"subtitles", anitomy::kElementSubtitles},
    {"video_resolution", anitomy::kElementVideoResolution}, {"video_term", anitomy::kElementVideoTerm},
    {"volume_number", anitomy::kElementVolumeNumber}
};
QJsonObject extract(const QString& filename, const QJsonObject& options = {}) {
    anitomy::Anitomy parser;
    if (options.contains("option_allowed_delimiters"))
        parser.options().allowed_delimiters = options.value("option_allowed_delimiters").toString().toStdWString();
    for (const auto& value : options.value("option_ignored_strings").toArray())
        parser.options().ignored_strings.push_back(value.toString().toStdWString());
    QJsonObject result{{"parseReturned", parser.Parse(filename.toStdWString())}};
    for (const auto& field : fields) {
        QJsonArray values;
        for (const auto& value : parser.elements().get_all(field.category))
            values.append(QString::fromStdWString(value));
        if (!values.isEmpty()) result[field.name] = values;
    }
    return result;
}
QString first(const QJsonObject& object, const char* field) {
    const auto values = object.value(field).toArray();
    return values.isEmpty() ? QString() : values.at(0).toString();
}
QString basename(QString title) {
    // Same input boundary as the current parser. Do not split Fate/stay night.
    static const QRegularExpression path(QStringLiteral(R"(^(?:[a-z]:[\\/]|\\\\))"), QRegularExpression::CaseInsensitiveOption);
    if (path.match(title).hasMatch()) title = title.section(QRegularExpression(QStringLiteral("[/\\\\]")), -1);
    return title;
}
QJsonObject current(const QString& filename) {
    const auto value = parseRuleMediaTitle(filename); // Frozen pre-integration baseline.
    return {{"valid", value.valid()}, {"title", value.anime}, {"key", value.animeKey},
        {"episode", value.episode}, {"explicitEpisode", value.explicitEpisode}};
}
int auditLibrary(const QString& inventoryPath, const QString& outputPath) {
    QFile inventory(inventoryPath);
    if (!inventory.open(QIODevice::ReadOnly)) { qCritical("Cannot open library inventory"); return 2; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(inventory.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("files").isArray()) {
        qCritical("Invalid library inventory"); return 2;
    }
    const auto source = document.object();
    QJsonArray rows;
    QJsonObject issueCounts;
    int currentFields = 0, anitomyFields = 0, bothFields = 0, episodeConflicts = 0, titleDifferences = 0;
    int hybridResolved = 0, hybridNeedsConfirmation = 0;
    QElapsedTimer timer; timer.start();
    for (const auto& value : source.value("files").toArray()) {
        const auto item = value.toObject();
        const auto filename = item.value("filename").toString();
        if (filename.isEmpty()) { qCritical("Inventory entry is missing its filename"); return 2; }
        const auto input = basename(filename).normalized(QString::NormalizationForm_KC);
        const auto baseline = current(filename);
        const auto hybrid = identifyMediaTitle(filename);
        hybridResolved += hybrid.state == MediaParseResult::State::Resolved;
        hybridNeedsConfirmation += hybrid.state == MediaParseResult::State::NeedsConfirmation;
        const bool withinBudget = filename.size() <= 1024;
        const auto parsed = withinBudget ? extract(input) : QJsonObject();
        const auto episodes = parsed.value("episode_number").toArray();
        const auto title = first(parsed, "anime_title");
        bool numeric = false;
        const double episode = first(parsed, "episode_number").toDouble(&numeric);
        bool seasonNumeric = false;
        const auto seasonText = first(parsed, "anime_season");
        const int season = seasonText.toInt(&seasonNumeric);
        const bool seasonValid = seasonText.isEmpty() || (seasonNumeric && season > 0);
        const bool fieldsPresent = withinBudget && parsed.value("parseReturned").toBool() && !title.isEmpty() &&
            episodes.size() == 1 && numeric && std::isfinite(episode) && episode > 0 && episode <= 9999 && seasonValid;
        auto titleWithSeason = title;
        if (seasonNumeric && season > 1) titleWithSeason += QStringLiteral(" 第%1季").arg(season);
        const auto titleKey = canonicalAnimeTitle(titleWithSeason);
        const bool baselineFields = baseline.value("valid").toBool();
        const bool episodeConflict = fieldsPresent && baselineFields &&
            std::abs(episode - baseline.value("episode").toDouble()) > 0.0001;
        const bool titleDifference = fieldsPresent && baselineFields && titleKey != baseline.value("key").toString();
        QJsonArray issues;
        const auto note = [&](const char* name) {
            issues.append(name); issueCounts[name] = issueCounts.value(name).toInt() + 1;
        };
        if (!withinBudget) note("input_over_1024");
        if (!parsed.value("parseReturned").toBool()) note("anitomy_parse_false");
        if (title.isEmpty()) note("anitomy_missing_title");
        if (episodes.isEmpty()) note("anitomy_missing_episode");
        if (episodes.size() > 1) note("anitomy_multiple_episodes");
        if (episodes.size() == 1 && (!numeric || !std::isfinite(episode) || episode <= 0 || episode > 9999))
            note("anitomy_invalid_episode");
        if (!seasonValid) note("anitomy_invalid_season");
        if (episodeConflict) note("episode_conflict");
        if (titleDifference) note("title_difference");
        // Underscores separate release tags too: NCOP_EP12 and NCOP03_EP11.
        // These are audit warnings, not a production automatic-load policy.
        static const QRegularExpression special(QStringLiteral(R"((?:(?:^|[^\p{L}\p{N}])(?:SP|OVA|OAD|OAV|OP|ED|PV|CM|NCOP|NCED|MENU|MENUS|TRAILER|TEASER|SPECIALS?)\s*\d*(?=$|[^\p{L}\p{N}])|特别篇|特別篇|总集篇|總集篇|映像特典|予告|菜单|菜單))"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression extrasFolder(QStringLiteral(R"(^(?:SPs?|Extras?|Bonus|特典|映像特典|Menus?|CMs?|PVs?|OPs?|EDs?|NCOPs?|NCEDs?|NCOP&NCED)$)"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression range(QStringLiteral(R"(\d+(?:\.\d+)?\s*(?:话|話|集)?\s*[-~+&～至]\s*(?:EP(?:ISODE)?\s*|E|第\s*)?\d)"), QRegularExpression::CaseInsensitiveOption);
        const auto parent = item.value("parent").toString();
        const auto parentLeaf = QString(parent).replace('\\', '/').section('/', -1);
        if (special.match(input).hasMatch()) note("special_filename");
        if (extrasFolder.match(parentLeaf).hasMatch()) note("extras_folder");
        if (range.match(input).hasMatch()) note("range_like_filename");
        if (baselineFields && !baseline.value("explicitEpisode").toBool()) note("current_weak_episode");
        const auto group = first(parsed, "release_group");
        if (baselineFields && !group.isEmpty() && baseline.value("title").toString().contains('[' + group + ']', Qt::CaseInsensitive))
            note("current_keeps_release_group");
        currentFields += baselineFields; anitomyFields += fieldsPresent; bothFields += fieldsPresent && baselineFields;
        episodeConflicts += episodeConflict; titleDifferences += titleDifference;
        rows.append(QJsonObject{{"relativePath", item.value("relativePath")}, {"filename", filename}, {"parent", parent},
            {"current", baseline}, {"anitomy", parsed}, {"anitomySingleFields", fieldsPresent},
            {"hybrid", QJsonObject{{"resolved", hybrid.state == MediaParseResult::State::Resolved},
                {"valid", hybrid.selected.valid()}, {"title", hybrid.selected.anime}, {"episode", hybrid.selected.episode},
                {"reason", hybrid.reason}, {"searchTitle", hybrid.searchTitle}}},
            {"anitomyKey", titleKey}, {"episodeConflict", episodeConflict}, {"titleDifference", titleDifference}, {"issues", issues}});
    }
    QJsonObject result{{"scope", "Read-only filename extraction audit. Field availability is not accuracy or online match success. No parent title recovery or playback."},
        {"anitomyCommit", "a538eff670cb4666f8395cf66a9c29a535f2c383"}, {"observedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"root", source.value("root")}, {"inventoryScannedAtUtc", source.value("scannedAtUtc")},
        {"allFileCount", source.value("allFileCount")}, {"videoExtensions", source.value("videoExtensions")},
        {"scanErrors", source.value("scanErrors")}, {"videoFiles", rows.size()}, {"currentFields", currentFields},
        {"anitomySingleFields", anitomyFields}, {"bothFields", bothFields}, {"episodeConflicts", episodeConflicts},
        {"hybridResolved", hybridResolved}, {"hybridNeedsConfirmation", hybridNeedsConfirmation},
        {"titleDifferences", titleDifferences}, {"issueCounts", issueCounts}, {"elapsedMs", timer.elapsed()}, {"rows", rows}};
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) { qCritical("Cannot open library report"); return 2; }
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) { qCritical("Cannot save library report"); return 2; }
    qInfo("Audited %lld video filenames; Anitomy fields %d; current fields %d; episode conflicts %d",
        static_cast<long long>(rows.size()), anitomyFields, currentFields, episodeConflicts);
    return 0;
}
struct Sample {
    const char* label;
    QString filename, expectedTitle;
    double expectedEpisode{};
    int expectedSeason{};
    bool manual{};
};
QList<Sample> samples() {
    return {
        {"screenshot", "[KitaujiSub] Kimi ga Shinu made Koi wo Shitai [01][WebRip][HEVC_AAC][CHS_JP].mp4", "Kimi ga Shinu made Koi wo Shitai", 1},
        {"unknown-group", "[SomeNewTeam] Frieren - 02 [1080p][HEVC].mkv", "Frieren", 2},
        {"known-group", "[NC-Raws] Frieren - 01 [1080p][HEVC].mkv", "Frieren", 1},
        {"underscores", "[SubsPlease]_Frieren_-_03_(1080p)_[A1B2C3D4].mkv", "Frieren", 3},
        {"dotted", "Frieren.E04.1080p.WEB-DL.x265.mkv", "Frieren", 4},
        {"season", "Test S02E03.mp4", "Test", 3, 2},
        {"first-season", "Test S01E02.mkv", "Test", 2, 1},
        {"season-word", "Test Season 2 - 03.mkv", "Test", 3, 2},
        {"season-ordinal", "Test 2nd Season - 03.mkv", "Test", 3, 2},
        {"chinese-title", QStringLiteral("[未知字幕组] 葬送的芙莉莲 - 12 [1080p].mkv"), QStringLiteral("葬送的芙莉莲"), 12},
        {"chinese-episode", QStringLiteral("葬送的芙莉蓮 第 12 集"), QStringLiteral("葬送的芙莉蓮"), 12},
        {"chinese-brackets", QStringLiteral("[喵萌奶茶屋][葬送的芙莉莲][12][1080p][简日双语].mkv"), QStringLiteral("葬送的芙莉莲"), 12},
        {"chinese-season", QStringLiteral("Test 第二季 第3话.mkv"), "Test", 3, 2},
        {"chinese-numeral", QStringLiteral("Test 第十二话.mkv"), "Test", 12},
        {"japanese", QStringLiteral("[字幕組] きみが死ぬまで恋をしたい - 01.mkv"), QStringLiteral("きみが死ぬまで恋をしたい"), 1},
        {"decimal", "Test EP12.5v2", "Test", 12.5},
        {"release-version", "[Group] Test - 01v2 [1080p].mkv", "Test", 1},
        {"drive-path", "D:\\Anime\\[KitaujiSub] Test - 02.mkv", "Test", 2},
        {"unc-path", "\\\\server\\share\\[KitaujiSub] Test - 02.mkv", "Test", 2},
        {"slash-in-title", "[Group] Fate/stay night - 01.mkv", "Fate/stay night", 1},
        {"numeric-title", "86 - 01.mkv", "86", 1},
        {"numeric-suffix-title", "[Group] Code Geass R2 - 01.mkv", "Code Geass R2", 1},
        {"unicode-supplementary", QString::fromUcs4(U"[Group] 番劇𠮷 - 01.mkv"), QString::fromUcs4(U"番劇𠮷"), 1},
        {"fullwidth", QStringLiteral("[Group] Test ＥＰ０１.mkv"), "Test", 1},
        // These are observations, not permission to automatically load a normal episode.
        {"range", "Test - 01-02.mkv", {}, 0, 0, true},
        {"range-spaced", "Test - 01 - 02.mkv", {}, 0, 0, true},
        {"range-bracket", "Test [01-12].mkv", {}, 0, 0, true},
        {"multiple-episodes", "Test - 01 & 02.mkv", {}, 0, 0, true},
        {"ova", "Test OVA 01.mkv", {}, 0, 0, true},
        {"special", "Test SP01.mkv", {}, 0, 0, true},
        {"opening", "Test NCOP01.mkv", {}, 0, 0, true},
        {"ending", "Test ED01.mkv", {}, 0, 0, true},
        {"chinese-special", QStringLiteral("Test 特别篇 01.mkv"), {}, 0, 0, true},
        {"zero-season", "Test S00E01.mkv", {}, 0, 0, true},
        {"zero-episode", "Test - 00.mkv", {}, 0, 0, true},
        {"missing-episode", "Test.mkv", {}, 0, 0, true},
        {"weak-bare-number", "Test 12", {}, 0, 0, true},
        {"ambiguous-title", "Spice and Wolf 2", {}, 0, 0, true},
        {"numeric-only", "86", {}, 0, 0, true},
        {"empty", "", {}, 0, 0, true}
    };
}
}

class ProbeTests final : public QObject {
    Q_OBJECT
    QJsonObject report_;
private slots:
    void upstreamCorpus() {
        QFile file("upstream-data.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto corpus = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(corpus.size(), 182);
        QJsonArray mismatches;
        int checks = 0, matchingCases = 0, matchingIdentityCases = 0;
        for (const auto& item : corpus) {
            const auto expected = item.toObject();
            const auto name = expected.value("file_name").toString();
            const auto actual = extract(name, expected);
            bool caseMatches = true, identityMatches = true;
            for (const auto& field : fields) {
                if (!expected.contains(field.name) || QByteArray(field.name) == "file_name") continue;
                ++checks;
                const auto expectedValues = expected.value(field.name).isArray()
                    ? expected.value(field.name).toArray() : QJsonArray{expected.value(field.name)};
                if (actual.value(field.name).toArray() != expectedValues) {
                    caseMatches = false;
                    if (QByteArray(field.name) == "anime_title" || QByteArray(field.name) == "episode_number" || QByteArray(field.name) == "anime_season")
                        identityMatches = false;
                    mismatches.append(QJsonObject{{"input", name}, {"field", field.name},
                        {"expected", expected.value(field.name)}, {"actual", actual.value(field.name)}});
                }
            }
            matchingCases += caseMatches; matchingIdentityCases += identityMatches;
        }
        report_["upstream"] = QJsonObject{{"cases", corpus.size()}, {"fieldChecks", checks},
            {"matchingCases", matchingCases}, {"matchingIdentityCases", matchingIdentityCases}, {"mismatches", mismatches}};
        // The upstream corpus is an evaluation dataset, not an assertion that every
        // naming convention works. Keep all discrepancies in the report.
        QVERIFY(checks > 1000);
        qInfo("Upstream evaluation: %d/%lld full matches, %d/%lld title/season/episode matches, %lld field discrepancies",
            matchingCases, static_cast<long long>(corpus.size()), matchingIdentityCases,
            static_cast<long long>(corpus.size()), static_cast<long long>(mismatches.size()));
    }
    void comparisonMatrix() {
        QJsonArray rows;
        int upstreamMatches = 0, currentMatches = 0, positiveCount = 0;
        for (const auto& sample : samples()) {
            // Production integration would need the same path and Unicode adapter.
            const auto adapted = basename(sample.filename).normalized(QString::NormalizationForm_KC);
            const auto raw = extract(sample.filename);
            const auto adaptedResult = extract(adapted);
            const auto baseline = current(sample.filename);
            QJsonObject row{{"label", sample.label}, {"input", sample.filename}, {"manualRequired", sample.manual},
                {"rawAnitomy", raw}, {"pathAndNfkcAdaptedAnitomy", adaptedResult}, {"current", baseline}};
            if (!sample.manual) {
                ++positiveCount;
                const auto episodeValues = adaptedResult.value("episode_number").toArray();
                const int season = first(adaptedResult, "anime_season").toInt();
                const bool matched = adaptedResult.value("parseReturned").toBool() &&
                    canonicalAnimeTitle(first(adaptedResult, "anime_title")) == canonicalAnimeTitle(sample.expectedTitle) && episodeValues.size() == 1 &&
                    std::abs(first(adaptedResult, "episode_number").toDouble() - sample.expectedEpisode) < 0.0001 &&
                    (sample.expectedSeason <= 1 ? season <= 1 : season == sample.expectedSeason);
                auto expectedCanonical = sample.expectedTitle;
                if (sample.expectedSeason > 1) expectedCanonical += QStringLiteral(" 第%1季").arg(sample.expectedSeason);
                const bool baselineMatched = baseline.value("key").toString() == canonicalAnimeTitle(expectedCanonical) &&
                    std::abs(baseline.value("episode").toDouble() - sample.expectedEpisode) < 0.0001;
                upstreamMatches += matched; currentMatches += baselineMatched;
                row["expected"] = QJsonObject{{"title", sample.expectedTitle}, {"season", sample.expectedSeason}, {"episode", sample.expectedEpisode}};
                row["anitomyExtractionMatches"] = matched;
                row["currentExtractionMatches"] = baselineMatched;
            }
            rows.append(row);
        }
        report_["comparison"] = QJsonObject{{"cases", rows.size()}, {"positiveCases", positiveCount},
            {"anitomyMatches", upstreamMatches}, {"currentMatches", currentMatches}, {"rows", rows}};
        QCOMPARE(rows.size(), 40);
    }
    void screenshotAndUnicodeBridge() {
        const auto sample = samples().first();
        const auto parsed = extract(sample.filename);
        QCOMPARE(first(parsed, "anime_title"), sample.expectedTitle);
        QCOMPARE(first(parsed, "episode_number"), QString("01"));
        QCOMPARE(first(parsed, "release_group"), QString("KitaujiSub"));
        const auto unicode = QString::fromUcs4(U"[字幕組] 番劇𠮷 - 01.mkv");
        QCOMPARE(QString::fromStdWString(unicode.toStdWString()), unicode);
        QCOMPARE(first(extract(unicode), "anime_title"), QString::fromUcs4(U"番劇𠮷"));
    }
    void noOnlineAliasDiscovery() {
        const auto parsed = extract(samples().first().filename);
        // A filename parser extracts spelling; it does not resolve database identity.
        QCOMPARE(first(parsed, "anime_title"), QString("Kimi ga Shinu made Koi wo Shitai"));
        QVERIFY(canonicalAnimeTitle(first(parsed, "anime_title")) != canonicalAnimeTitle(QStringLiteral("与你相恋到生命尽头")));
    }
    void integrationHazards() {
        // Preserve evidence of boundaries that an integration must handle itself.
        QCOMPARE(extract("Test - 01-02.mkv").value("episode_number").toArray().size(), 2);
        QCOMPARE(first(extract("Test EP12.5v2"), "episode_number"), QString("12"));
        QCOMPARE(parseMediaTitle("Test EP12.5v2").episode, 12.5);
        QVERIFY(!parseMediaTitle("Test - 01-02.mkv").valid());
        QVERIFY(!parseMediaTitle("Test SP01.mkv").valid());
        QVERIFY(!parseMediaTitle("Test S00E01.mkv").valid());
        QCOMPARE(first(extract("Spice and Wolf 2"), "episode_number"), QString("2"));
        QVERIFY(!parseMediaTitle("Spice and Wolf 2").explicitEpisode);
        const auto chinese = QStringLiteral("葬送的芙莉蓮 第 12 集");
        QVERIFY(first(extract(chinese), "anime_title") != QStringLiteral("葬送的芙莉蓮"));
        QCOMPARE(parseMediaTitle(chinese).episode, 12.0);
    }
    void cleanupTestCase() {
        report_["scope"] = "Isolated C++20 extraction probe; no automatic playback, online matching or production parser replacement";
        report_["anitomyCommit"] = "a538eff670cb4666f8395cf66a9c29a535f2c383";
        report_["qtVersion"] = qVersion();
        report_["cppStandard"] = static_cast<double>(__cplusplus);
        report_["wcharBits"] = static_cast<int>(sizeof(wchar_t) * 8);
#ifdef QT_DEBUG
        report_["configuration"] = "Debug";
#else
        report_["configuration"] = "Release";
#endif
        const auto path = qEnvironmentVariable("DANMAX_ANITOMY_REPORT", "comparison.json");
        QSaveFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(report_).toJson(QJsonDocument::Indented);
        QCOMPARE(file.write(bytes), bytes.size()); QVERIFY(file.commit());
    }
};
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.contains("--inventory")) {
        const int input = arguments.indexOf("--inventory"), output = arguments.indexOf("--audit-report");
        if (input + 1 >= arguments.size() || output < 0 || output + 1 >= arguments.size()) {
            qCritical("Usage: anitomy_probe --inventory inventory.json --audit-report report.json"); return 2;
        }
        return auditLibrary(arguments.at(input + 1), arguments.at(output + 1));
    }
    ProbeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ProbeTests.moc"
