// core/platform/file_dialog_logic.h
// 文件对话框可纯测逻辑：扩展名归一与过滤器拼装（打开/保存共用）+ 保存侧专属归一。
// 单独成头的原因：platform.cpp 的对话框助手原本全在匿名命名空间，单测够不着；
// 沿用 e066ef4（window_effect.h + tests/unit/window_effect_backdrop_state）的纯函数抽头拆法。
#pragma once

#include "core/platform/platform.h"

#include <algorithm>
#include <string>
#include <vector>

namespace core::platform {

inline std::string trimWhitespace(std::string value) {
    const auto first = std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
    });
    const auto last = std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

// "txt" / " txt " / ".txt" / "*.txt" 一律归一成 "txt"；空串原样返回。
inline std::string extensionName(std::string value) {
    value = trimWhitespace(std::move(value));
    if (value.rfind("*.", 0) == 0) {
        value.erase(0, 2);
    } else if (!value.empty() && value.front() == '.') {
        value.erase(value.begin());
    }
    return value;
}

// 归一并丢弃通配/空项："*" 与 "" 不构成有意义的过滤器。
inline std::vector<std::string> extensionNames(const std::vector<std::string>& extensions) {
    std::vector<std::string> result;
    for (const std::string& extension : extensions) {
        std::string name = extensionName(extension);
        if (name.empty() || name == "*") {
            continue;
        }
        result.push_back(std::move(name));
    }
    return result;
}

inline std::string extensionPattern(const std::string& extensionName) {
    return "*." + extensionName;
}

inline std::vector<std::string> extensionPatterns(const std::vector<std::string>& names) {
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const std::string& name : names) {
        result.push_back(extensionPattern(name));
    }
    return result;
}

// 过滤器显示名兜底；Windows 过滤器存储与 Linux 过滤器参数都经这里取名字。
inline std::string fileFilterName(const FileDialogOptions& options) {
    return options.filterName.empty() ? "Allowed files" : options.filterName;
}

// 保存对话框标题兜底，与打开侧 "Select file" 同风格。
inline std::string saveFileDialogTitle(const SaveFileDialogOptions& options) {
    return options.prompt.empty() ? "Save file" : options.prompt;
}

// 保存选项 → 打开选项：逐字段搬运，让 windowsFilterStorage / linuxFilterArgument /
// zenity / kdialog 的过滤器拼装直接复用打开侧实现，不另写平行版本。
inline FileDialogOptions asOpenOptions(const SaveFileDialogOptions& options) {
    FileDialogOptions result;
    result.prompt = options.prompt;
    result.allowedExtensions = options.allowedExtensions;
    result.initialDirectory = options.initialDirectory;
    result.filterName = options.filterName;
    return result;
}

// 默认扩展名补齐：文件名主干没有 '.' 才追加（"note"→"note.txt"；"note.txt"、".note"
// 原样保留——隐藏文件按约定不算缺扩展名）。只看最后一段路径，目录带点（"a.b/note"）
// 不误判。扩展名先过 extensionName 归一，容忍 ".txt" / "*.txt" 形态。Windows 的
// lpstrDefExt 在对话框内已补齐，这里幂等兜底，macOS/Linux 则靠它走同一契约。
inline std::string applyDefaultExtension(const std::string& path, const std::string& extension) {
    const std::string name = extensionName(extension);
    if (path.empty() || name.empty() || name == "*") {
        return path;
    }
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (separator == std::string::npos || dot > separator)) {
        return path;
    }
    return path + "." + name;
}

#if defined(__APPLE__)
// AppleScript 字符串字面量转义：反斜杠与双引号都要补 \，否则路径含引号会截断脚本。
inline std::string appleScriptString(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

// choose file name 脚本行（不含 osascript 命令包装）。choose file name 没有类型过滤
// 参数——保存侧忽略 allowedExtensions 是平台能力缺口，不是实现缺省；default name 与
// default location 只在有值时带上，缺省时交给系统默认。
inline std::vector<std::string> appleSaveFileDialogScript(const SaveFileDialogOptions& options) {
    std::string choose = "set eui_path to choose file name with prompt \"" +
        appleScriptString(saveFileDialogTitle(options)) + "\"";
    if (!options.defaultName.empty()) {
        choose += " default name \"" + appleScriptString(options.defaultName) + "\"";
    }
    if (!options.initialDirectory.empty()) {
        choose += " default location POSIX file \"" + appleScriptString(options.initialDirectory) + "\"";
    }
    return {choose, "POSIX path of eui_path"};
}
#endif

}  // namespace core::platform
