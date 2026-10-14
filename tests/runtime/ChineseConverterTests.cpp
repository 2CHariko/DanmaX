#include "infrastructure/ChineseConverter.h"
#include "application/AppController.h"
#include "infrastructure/SettingsStore.h"
#include <QTemporaryDir>
#include <QTest>

class ChineseConverterTests final : public QObject {
    Q_OBJECT
private slots:
    void testSimplifiedConversion() {
        // 繁体转简体
        const QString traditional = QStringLiteral("葬送的芙莉蓮，這是一條繁體彈幕，測試轉換！");
        const QString simplified = danmax::toSimplified(traditional);
        QCOMPARE(simplified, QStringLiteral("葬送的芙莉莲，这是一条繁体弹幕，测试转换！"));

        // std::string 版本
        const std::string stdTrad = "後會有期，記憶體與網絡";
        const std::string stdSimp = danmax::toSimplified(stdTrad);
        // LCMapStringEx 基础字形转换
        QVERIFY(!stdSimp.empty());
    }

    void testTraditionalConversion() {
        // 简体转繁体
        const QString simplified = QStringLiteral("葬送的芙莉莲，这是一条繁体弹幕，测试转换！");
        const QString traditional = danmax::toTraditional(simplified);
        QCOMPARE(traditional, QStringLiteral("葬送的芙莉蓮，這是一條繁體彈幕，測試轉換！"));
    }

    void testModeHandling() {
        const QString sample = QStringLiteral("繁體中文");
        QCOMPARE(danmax::convertChinese(sample, danmax::ChineseConversion::Off), sample);
        QCOMPARE(danmax::convertChinese(sample, danmax::ChineseConversion::Simplified), QStringLiteral("繁体中文"));
        QCOMPARE(danmax::convertChinese(QStringLiteral("繁体中文"), danmax::ChineseConversion::Traditional), QStringLiteral("繁體中文"));

        QCOMPARE(danmax::parseChineseConversion(QStringLiteral("off")), danmax::ChineseConversion::Off);
        QCOMPARE(danmax::parseChineseConversion(QStringLiteral("simplified")), danmax::ChineseConversion::Simplified);
        QCOMPARE(danmax::parseChineseConversion(QStringLiteral("traditional")), danmax::ChineseConversion::Traditional);
        QCOMPARE(danmax::parseChineseConversion(QStringLiteral("unknown")), danmax::ChineseConversion::Off);

        QCOMPARE(danmax::chineseConversionToString(danmax::ChineseConversion::Off), QStringLiteral("off"));
        QCOMPARE(danmax::chineseConversionToString(danmax::ChineseConversion::Simplified), QStringLiteral("simplified"));
        QCOMPARE(danmax::chineseConversionToString(danmax::ChineseConversion::Traditional), QStringLiteral("traditional"));
    }

    void testAsciiAndEmpty() {
        QCOMPARE(danmax::toSimplified(QString{}), QString{});
        QCOMPARE(danmax::toTraditional(QString{}), QString{});
        QCOMPARE(danmax::toSimplified(QStringLiteral("Hello World 123!@#")), QStringLiteral("Hello World 123!@#"));
        QCOMPARE(danmax::toTraditional(QStringLiteral("Hello World 123!@#")), QStringLiteral("Hello World 123!@#"));
    }
    void testSettingsStoreIntegration() {
        QTemporaryDir dir;
        SettingsStore store(dir.path());
        QCOMPARE(store.values().value("chineseConversion").toString(), QStringLiteral("off"));

        QVERIFY(store.setValue(QStringLiteral("chineseConversion"), QStringLiteral("simplified")));
        QCOMPARE(store.values().value("chineseConversion").toString(), QStringLiteral("simplified"));

        QVERIFY(store.setValue(QStringLiteral("chineseConversion"), QStringLiteral("traditional")));
        QCOMPARE(store.values().value("chineseConversion").toString(), QStringLiteral("traditional"));

        // 非法值应被拒绝
        QVERIFY(!store.setValue(QStringLiteral("chineseConversion"), QStringLiteral("invalid_mode")));
        QCOMPARE(store.values().value("chineseConversion").toString(), QStringLiteral("traditional"));
    }

    void testAppControllerConversion() {
        QTemporaryDir dir;
        AppController controller(dir.path());
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        QVERIFY(settings);

        // 设置为转简体
        settings->setValue(QStringLiteral("chineseConversion"), QStringLiteral("simplified"));

        const QString xmlPath = dir.filePath("test_danmaku.xml");
        QFile file(xmlPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        const QString xmlContent = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<i>\n"
            "  <d p=\"0.0,1,25,16777215,1600000000,0,0,0\">葬送的芙莉蓮，這是一條繁體彈幕！</d>\n"
            "</i>\n"
        );
        file.write(xmlContent.toUtf8());
        file.close();

        controller.loadFile(xmlPath);
        QTRY_COMPARE(controller.total(), 1);

        // 动态将设置切换为繁体
        settings->setValue(QStringLiteral("chineseConversion"), QStringLiteral("traditional"));
        // 动态将设置切换回保持原样
        settings->setValue(QStringLiteral("chineseConversion"), QStringLiteral("off"));
    }
};

QTEST_MAIN(ChineseConverterTests)
#include "ChineseConverterTests.moc"
