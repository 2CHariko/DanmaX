#include "infrastructure/ChineseConverter.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace danmax {

namespace {

#ifdef _WIN32
QString mapString(const QString& text, DWORD mapFlags) {
    if (text.isEmpty()) {
        return text;
    }
    QString result(text.size(), Qt::Uninitialized);
    const int written = LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        mapFlags,
        reinterpret_cast<LPCWSTR>(text.utf16()),
        static_cast<int>(text.size()),
        reinterpret_cast<LPWSTR>(result.data()),
        static_cast<int>(result.size()),
        nullptr,
        nullptr,
        0
    );
    if (written > 0) {
        result.resize(written);
        return result;
    }

    const int required = LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        mapFlags,
        reinterpret_cast<LPCWSTR>(text.utf16()),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0
    );
    if (required <= 0) {
        return text;
    }

    result.resize(required);
    const int secondWritten = LCMapStringEx(
        LOCALE_NAME_USER_DEFAULT,
        mapFlags,
        reinterpret_cast<LPCWSTR>(text.utf16()),
        static_cast<int>(text.size()),
        reinterpret_cast<LPWSTR>(result.data()),
        required,
        nullptr,
        nullptr,
        0
    );
    if (secondWritten > 0) {
        result.resize(secondWritten);
        return result;
    }
    return text;
}
#else
QString mapString(const QString& text, unsigned long) {
    return text;
}
#endif

} // namespace

ChineseConversion parseChineseConversion(const QString& str) {
    const auto lower = str.trimmed().toLower();
    if (lower == QStringLiteral("simplified") || lower == QStringLiteral("1")) {
        return ChineseConversion::Simplified;
    }
    if (lower == QStringLiteral("traditional") || lower == QStringLiteral("2")) {
        return ChineseConversion::Traditional;
    }
    return ChineseConversion::Off;
}

QString chineseConversionToString(ChineseConversion mode) {
    switch (mode) {
    case ChineseConversion::Simplified:
        return QStringLiteral("simplified");
    case ChineseConversion::Traditional:
        return QStringLiteral("traditional");
    case ChineseConversion::Off:
    default:
        return QStringLiteral("off");
    }
}

QString toSimplified(const QString& text) {
#ifdef _WIN32
    return mapString(text, LCMAP_SIMPLIFIED_CHINESE);
#else
    return text;
#endif
}

std::string toSimplified(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const auto qstr = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    const auto converted = toSimplified(qstr);
    const auto utf8 = converted.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString toTraditional(const QString& text) {
#ifdef _WIN32
    return mapString(text, LCMAP_TRADITIONAL_CHINESE);
#else
    return text;
#endif
}

std::string toTraditional(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const auto qstr = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    const auto converted = toTraditional(qstr);
    const auto utf8 = converted.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString convertChinese(const QString& text, ChineseConversion mode) {
    switch (mode) {
    case ChineseConversion::Simplified:
        return toSimplified(text);
    case ChineseConversion::Traditional:
        return toTraditional(text);
    case ChineseConversion::Off:
    default:
        return text;
    }
}

std::string convertChinese(std::string_view text, ChineseConversion mode) {
    switch (mode) {
    case ChineseConversion::Simplified:
        return toSimplified(text);
    case ChineseConversion::Traditional:
        return toTraditional(text);
    case ChineseConversion::Off:
    default:
        return std::string(text);
    }
}

} // namespace danmax
