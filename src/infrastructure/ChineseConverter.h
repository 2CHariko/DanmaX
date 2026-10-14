#pragma once
#include <QString>
#include <string>
#include <string_view>

namespace danmax {

enum class ChineseConversion {
    Off,
    Simplified,
    Traditional
};

[[nodiscard]] ChineseConversion parseChineseConversion(const QString& str);
[[nodiscard]] QString chineseConversionToString(ChineseConversion mode);

[[nodiscard]] QString toSimplified(const QString& text);
[[nodiscard]] std::string toSimplified(std::string_view text);

[[nodiscard]] QString toTraditional(const QString& text);
[[nodiscard]] std::string toTraditional(std::string_view text);

[[nodiscard]] QString convertChinese(const QString& text, ChineseConversion mode);
[[nodiscard]] std::string convertChinese(std::string_view text, ChineseConversion mode);

} // namespace danmax
