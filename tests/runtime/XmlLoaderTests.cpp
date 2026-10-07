#include "infrastructure/XmlLoader.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>
#include <algorithm>

class XmlLoaderTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QString write(const QByteArray& bytes) {
        const auto path = directory_.filePath("input.xml");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return {};
        return path;
    }

  private slots:
    void bilibiliFieldsAndModes() {
        const auto result = readDanmakuXml(write(QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><i><chatid>1176840</chatid>"
            "<d p=\"2.125,5,36,16750950,1472119608,3,79f5e374,1294929134430238720\">顶部😀</d>"
            "<d p=\"0,1,25,255,1,0,user,2\">中文 &amp; &lt; &#x1F600;</d>"
            "<d p=\"0,2,18,0\">第二种滚动</d><d p=\"0,3,30,65280\">第三种滚动</d>"
            "<d p=\"1,4,25,16777215\"><![CDATA[底部 <文字>]]></d>"
            "<d p=\"1,6,25,0\">reverse</d><d p=\"1,7,100,0\">[0,0,\"1\",4,\"advanced\"]</d>"
            "<d p=\"1,8,25,0\">script</d><d p=\"1,9,25,0\">unknown</d></i>").toUtf8()));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(5));
        QCOMPARE(result.skipped, 4);
        QCOMPARE(result.unsupportedModes, 4);
        QCOMPARE(result.invalidRecords, 0);
        QCOMPARE(result.items[0].text, QStringLiteral("中文 & < 😀").toUtf8().toStdString());
        QCOMPARE(result.items[0].color, std::uint32_t(255));
        QCOMPARE(result.items[1].mode, danmaku::Mode::Scroll);
        QCOMPARE(result.items[1].fontSize, 18);
        QCOMPARE(result.items[2].mode, danmaku::Mode::Scroll);
        QCOMPARE(result.items[3].mode, danmaku::Mode::Bottom);
        QCOMPARE(result.items[3].text, QStringLiteral("底部 <文字>").toUtf8().toStdString());
        QCOMPARE(result.items[4].time, 2.125);
        QCOMPARE(result.items[4].mode, danmaku::Mode::Top);
        QCOMPARE(result.items[4].color, std::uint32_t(16750950));
        QCOMPARE(result.items[4].fontSize, 36);
    }

    void forbiddenControlsAndChunkBoundaries() {
        QByteArray xml("<i><!--");
        // Put U+0016 at the end of a device chunk, before a multibyte UTF-8 text.
        xml += QByteArray(65535 - xml.size(), 'x');
        xml += char(0x16);
        xml += "--><d p=\"0,7,25,0\">advanced";
        int repaired = 1;
        for (int code = 0; code < 32; ++code) {
            if (code == 9 || code == 10 || code == 13) continue;
            xml += static_cast<char>(code);
            ++repaired;
        }
        xml += "</d><d p=\"1,1,25,0\">";
        xml += QStringLiteral("正常😀").toUtf8();
        xml += char(0x16);
        xml += "text\nnext\tcolumn</d></i>";
        ++repaired;
        const auto path = write(xml);
        const auto result = readDanmakuXml(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(1));
        QCOMPARE(result.unsupportedModes, 1);
        QCOMPARE(result.sanitizedCharacters, repaired);
        QCOMPARE(result.items[0].text, QStringLiteral("正常😀 text next\tcolumn").toUtf8().toStdString());
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), xml); // Sanitation must never rewrite the source file.
    }

    void utf16_data() {
        QTest::addColumn<bool>("littleEndian");
        QTest::addColumn<bool>("bom");
        QTest::newRow("LE BOM") << true << true;
        QTest::newRow("BE BOM") << false << true;
        QTest::newRow("LE declaration") << true << false;
        QTest::newRow("BE declaration") << false << false;
    }
    void utf16() {
        QFETCH(bool, littleEndian);
        QFETCH(bool, bom);
        auto text = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-16\"?><i><d p=\"0,1,25,0\">中文😀");
        if (!bom) text.replace("UTF-16", littleEndian ? "UTF-16LE" : "UTF-16BE");
        text += QChar(0x16);
        text += QStringLiteral("正常</d></i>");
        QByteArray bytes;
        if (bom) bytes += littleEndian ? QByteArray::fromHex("fffe") : QByteArray::fromHex("feff");
        for (const auto unit : text) {
            const auto value = unit.unicode();
            bytes += static_cast<char>(littleEndian ? value & 255 : value >> 8);
            bytes += static_cast<char>(littleEndian ? value >> 8 : value & 255);
        }
        const auto result = readDanmakuXml(write(bytes));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(1));
        QCOMPARE(result.items[0].text, QStringLiteral("中文😀 正常").toUtf8().toStdString());
        QCOMPARE(result.sanitizedCharacters, 1);
    }

    void rejectsStructuralErrors_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("truncated") << QByteArray("<i><d p=\"0,1,25,0\">ok</d><d>");
        QTest::newRow("mismatched") << QByteArray("<i><d p=\"0,1,25,0\">ok</d></wrong>");
        QTest::newRow("nested text") << QByteArray("<i><d p=\"0,1,25,0\"><b>text</b></d></i>");
        QTest::newRow("entity") << QByteArray("<i><d p=\"0,1,25,0\">&unknown;</d></i>");
        QTest::newRow("DTD") << QByteArray("<!DOCTYPE i [<!ENTITY x 'x'>]><i/>");
        QTest::newRow("UTF-8") << (QByteArray("<i><d p=\"0,1,25,0\">") + QByteArray::fromHex("ff") + "</d></i>");
        QTest::newRow("escaped control") << QByteArray("<i><d p=\"0,1,25,0\">&#x16;</d></i>");
    }
    void rejectsStructuralErrors() {
        QFETCH(QByteArray, bytes);
        const auto result = readDanmakuXml(write(bytes));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.items.empty());
        QVERIFY(!result.cancelled);
    }

    void invalidRecordsAndProgress() {
        QByteArray xml("<i><d p=\"0,1,25,0\">valid</d>");
        for (const auto& p : {"", "nan,1,25,0", "inf,1,25,0", "-1,1,25,0", "604801,1,25,0",
                              "0,x,25,0", "0,1,0,0", "0,1,201,0", "0,1,25,-1", "0,1,25,16777216"})
            xml += QByteArray("<d p=\"") + p + "\">bad</d>";
        xml += "<d p=\"0,1,25,0\"> </d><d p=\"0,1,25,0\">";
        xml += QByteArray(513, 'x');
        xml += "</d>";
        xml += QByteArray(70000, ' ');
        xml += "<d p=\"0,7,25,0\">unsupported</d></i>";
        std::vector<int> progress;
        const auto result = readDanmakuXml(write(xml), {}, [&](int value) { progress.push_back(value); });
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(1));
        QCOMPARE(result.invalidRecords, 12);
        QCOMPARE(result.unsupportedModes, 1);
        QCOMPARE(result.skipped, 13);
        QVERIFY(std::find(progress.begin(), progress.end(), 100) != progress.end());
        QCOMPARE(progress.back(), -1);
    }

    void cancellation() {
        const auto path = write("<i><d p=\"0,1,25,0\">first</d><d p=\"1,1,25,0\">next</d></i>");
        std::stop_source source;
        const auto result = readDanmakuXml(path, source.get_token(), [&](int) { source.request_stop(); });
        QVERIFY(result.cancelled);
        QVERIFY(result.items.empty());
        QVERIFY(result.error.isEmpty());
        QCOMPARE(readDanmakuXml(path, source.get_token()).cancelled, true);
    }

    void missingColorFallback() {
        const auto result = readDanmakuXml(write(
            "<i><d p=\"0,1,25,undefined\">missing</d><d p=\"1,4,18,NULL\">null</d>"
            "<d p=\"2,5,36,\">empty</d><d p=\"3,1,25,0\">black</d>"
            "<d p=\"4,1,25\">absent field</d><d p=\"5,1,25,garbage\">invalid</d>"
            "<d p=\"6,1,25,-1\">negative</d><d p=\"7,1,25,16777216\">overflow</d>"
            "<d p=\"8,7,25,undefined\">advanced</d></i>"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(4));
        QCOMPARE(result.defaultedColors, 3);
        QCOMPARE(result.invalidRecords, 4);
        QCOMPARE(result.unsupportedModes, 1);
        for (int i = 0; i < 3; ++i) QCOMPARE(result.items[i].color, std::uint32_t(0xffffff));
        QCOMPARE(result.items[3].color, std::uint32_t(0));
    }

    void limitsIncludeUnsupportedRecords() {
        QByteArray xml("<i>");
        const QByteArray record("<d p=\"0,7,25,0\">x</d>");
        xml.reserve(record.size() * 1000001 + 7);
        for (int i = 0; i < 1000001; ++i) xml += record;
        xml += "</i>";
        const auto result = readDanmakuXml(write(xml));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.items.empty());
        QCOMPARE(result.unsupportedModes, 1000000);
    }

    void providedBilibiliDownload() {
        const auto path = qEnvironmentVariable("DANMAKU_BILIBILI_SAMPLE");
        if (path.isEmpty()) QSKIP("Set DANMAKU_BILIBILI_SAMPLE to validate the user-provided download.");
        QElapsedTimer timer;
        timer.start();
        const auto result = readDanmakuXml(path);
        const auto elapsed = timer.elapsed();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.cancelled);
        QCOMPARE(result.items.size(), std::size_t(16640));
        QCOMPARE(result.unsupportedModes, 862);
        QCOMPARE(result.invalidRecords, 0);
        QCOMPARE(result.sanitizedCharacters, 1);
        QCOMPARE(result.defaultedColors, 75);
        std::size_t scroll = 0, top = 0, bottom = 0;
        for (const auto& item : result.items) {
            if (item.mode == danmaku::Mode::Scroll) ++scroll;
            if (item.mode == danmaku::Mode::Top) ++top;
            if (item.mode == danmaku::Mode::Bottom) ++bottom;
        }
        QCOMPARE(scroll, std::size_t(8613));
        QCOMPARE(top, std::size_t(6955));
        QCOMPARE(bottom, std::size_t(1072));
        QVERIFY(std::is_sorted(result.items.begin(), result.items.end(),
                               [](const auto& a, const auto& b) { return a.time < b.time; }));
        // Independently decode the XML to verify every accepted field and tie order.
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes = original.readAll();
        bytes.replace(char(0x16), ' ');
        QXmlStreamReader xml(bytes);
        std::vector<danmaku::Item> expected;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != u"d") continue;
            const auto fields = xml.attributes().value("p").toString().split(',');
            auto text = xml.readElementText().trimmed();
            const int mode = fields[1].toInt();
            if (mode == 7) continue;
            text.replace('\n', ' ');
            text.replace('\r', ' ');
            const auto color = fields[3] == "undefined" ? 0xffffffu : fields[3].toUInt();
            expected.push_back({fields[0].toDouble(), static_cast<danmaku::Mode>(mode),
                                text.toUtf8().toStdString(), color, fields[2].toInt()});
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        std::stable_sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        QCOMPARE(expected.size(), result.items.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(result.items[i].time, expected[i].time);
            QCOMPARE(result.items[i].mode, expected[i].mode);
            QCOMPARE(result.items[i].text, expected[i].text);
            QCOMPARE(result.items[i].color, expected[i].color);
            QCOMPARE(result.items[i].fontSize, expected[i].fontSize);
        }
        qInfo("Parsed download: %zu supported, %d unsupported, %d repaired; %lld ms (parser only)",
              result.items.size(), result.unsupportedModes, result.sanitizedCharacters, static_cast<long long>(elapsed));
    }
};

QTEST_GUILESS_MAIN(XmlLoaderTests)
#include "XmlLoaderTests.moc"
