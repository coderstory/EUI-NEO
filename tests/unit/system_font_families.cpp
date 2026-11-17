// need:266 —— systemFontFamilies() 枚举门禁 + 白名单外 family 名真解析断言。
// 运行输出（族总数 / 前 20 项 / 抽样解析路径）同时充当人工验证证据。
// EUI-NEO 单测是独立可执行（tests/unit 惯例，ctest -L unit 注册），随
// EUI_BUILD_APPS 构建面走（DevDesk 集成构建不启 EUI 单测，macOS 侧证据由
// DevDesk tests/unit/system_font_gate.cpp doctest 给出，两文件断言同构）。

#include "core/render/text.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

#if defined(_WIN32) || defined(__APPLE__)
bool hasFontFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::exists(path, error);
}
#endif

} // namespace

int main() {
    using core::TextPrimitive;

    int failures = 0;

    const std::vector<std::string> families = TextPrimitive::systemFontFamilies();
    std::cout << "system font families: " << families.size() << "\n";
    const size_t previewCount = std::min<size_t>(families.size(), 20);
    for (size_t i = 0; i < previewCount; ++i) {
        std::cout << "  [" << i << "] " << families[i] << "\n";
    }

#if defined(_WIN32) || defined(__APPLE__)
    // 1. 枚举非空（Windows/macOS 必有系统字体；其余平台返回空表属设计行为）。
    if (families.empty()) {
        std::cerr << "[1] enumeration returned empty vector on a platform with system fonts\n";
        ++failures;
    }

    // 2. 去重（升序排列后相邻项互异）且不含空串。
    if (!std::is_sorted(families.begin(), families.end())) {
        std::cerr << "[2] enumeration is not sorted\n";
        ++failures;
    }
    if (std::adjacent_find(families.begin(), families.end()) != families.end()) {
        std::cerr << "[2] enumeration contains duplicate family names\n";
        ++failures;
    }
    if (std::any_of(families.begin(), families.end(),
                    [](const std::string& family) { return family.empty(); })) {
        std::cerr << "[2] enumeration contains empty family names\n";
        ++failures;
    }

    // 3. 硬断言：抽 3 个族 resolveFontPath 必须解析到真实存在的字体文件
    //    （只列名不解析 = 假功能，不通过）。取位避免只抽到头部/尾部特殊族。
    if (!families.empty()) {
        const size_t samples[3] = {families.size() / 4, families.size() / 2,
                                   families.size() * 3 / 4};
        for (const size_t index : samples) {
            const std::string& family = families[index];
            const std::string path = TextPrimitive::resolveFontPath(family, 400);
            const bool exists = hasFontFile(path);
            std::cout << "  sample '" << family << "' -> "
                      << (path.empty() ? "<empty>" : path) << (exists ? " [exists]" : " [MISSING]")
                      << "\n";
            if (!exists) {
                std::cerr << "[3] family '" << family << "' did not resolve to an existing file\n";
                ++failures;
            }
        }
    }
#endif

    if (failures == 0) {
        std::cout << "system_font_families: ALL PASS\n";
    }
    return failures == 0 ? 0 : 1;
}
