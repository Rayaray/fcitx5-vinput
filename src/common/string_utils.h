#pragma once
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace vinput::str {

namespace detail {
template <typename T>
auto ToCArg(const T& v) {
    if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
        return v.c_str();
    } else {
        return v;
    }
}
} // namespace detail

template <typename... Args>
std::string FmtStr(const char* fmt, const Args&... args) {
    int n = std::snprintf(nullptr, 0, fmt, detail::ToCArg(args)...);
    if (n < 0) return fmt;
    std::string buf(static_cast<size_t>(n) + 1, '\0');
    std::snprintf(buf.data(), buf.size(), fmt, detail::ToCArg(args)...);
    buf.resize(static_cast<size_t>(n));
    return buf;
}

inline std::string FormatSize(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024 * 1024)
        return FmtStr("%.1f GB", bytes / (1024.0 * 1024 * 1024));
    if (bytes >= 1024ULL * 1024)
        return FmtStr("%.1f MB", bytes / (1024.0 * 1024));
    if (bytes >= 1024)
        return FmtStr("%.1f KB", bytes / 1024.0);
    return FmtStr("%llu B", (unsigned long long)bytes);
}

inline std::string TrimAsciiWhitespace(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        begin++;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }
    return std::string(text.substr(begin, end - begin));
}

} // namespace vinput::str
