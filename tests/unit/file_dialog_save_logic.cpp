// 保存文件对话框纯逻辑单测（need265）：扩展名归一 / 过滤器名与模式拼装 / 保存→打开
// 选项映射 / 默认扩展名补齐 / AppleScript 脚本行生成。
// 对话框本体（预填、取消、覆盖确认、中文路径）需真人交互，归 tests/fixtures/
// save_file_dialog_manual.cpp 人工验证，不在本文件覆盖。
#include "core/platform/file_dialog_logic.h"

#include <iostream>
#include <string>
#include <vector>

using namespace core::platform;

int main() {
    // 扩展名归一：裸名 / 带点 / 通配 / 空白
    if (extensionName("txt") != "txt" || extensionName(".md") != "md" ||
        extensionName("*.pdf") != "pdf" || extensionName("  log ") != "log" ||
        !extensionName("").empty()) {
        return 1;
    }
    // 归一并丢弃通配与空项
    if (extensionNames({"txt", ".md", "*.pdf", "*", "", "  txt2  "}) !=
        std::vector<std::string>({"txt", "md", "pdf", "txt2"})) {
        return 2;
    }
    // 过滤器模式串（Windows *.a;*.b / Linux 过滤参数的原料）
    if (extensionPatterns({"txt", "md"}) != std::vector<std::string>({"*.txt", "*.md"})) {
        return 3;
    }
    // 过滤器显示名兜底
    if (fileFilterName(FileDialogOptions{}) != "Allowed files") {
        return 4;
    }
    FileDialogOptions named;
    named.filterName = "文本文件";
    if (fileFilterName(named) != "文本文件") {
        return 5;
    }
    // 保存→打开选项映射：过滤器/目录/标题逐项搬运
    SaveFileDialogOptions source;
    source.prompt = "保存记事";
    source.defaultName = "未命名.txt";
    source.defaultExtension = "txt";
    source.allowedExtensions = {"txt", "md"};
    source.initialDirectory = "/tmp/有 空格目录";
    source.filterName = "文本";
    const FileDialogOptions mapped = asOpenOptions(source);
    if (mapped.prompt != "保存记事" || mapped.initialDirectory != "/tmp/有 空格目录" ||
        mapped.filterName != "文本" || mapped.allowedExtensions != source.allowedExtensions) {
        return 6;
    }
    // 默认扩展名补齐：缺则追加、有则不动、目录带点不误判、隐藏文件保留
    if (applyDefaultExtension("note", "txt") != "note.txt") {
        return 7;
    }
    if (applyDefaultExtension("note.txt", "txt") != "note.txt" ||
        applyDefaultExtension("a.b.txt", "txt") != "a.b.txt") {
        return 8;
    }
    if (applyDefaultExtension("a.b/note", "txt") != "a.b/note.txt" ||
        applyDefaultExtension("C:\\dir.d\\note", "txt") != "C:\\dir.d\\note.txt") {
        return 9;
    }
    if (applyDefaultExtension("笔记", "md") != "笔记.md" ||
        applyDefaultExtension(".note", "txt") != ".note") {
        return 10;
    }
    if (applyDefaultExtension("note", "") != "note" ||
        applyDefaultExtension("note", ".md") != "note.md" ||
        applyDefaultExtension("note", "*.pdf") != "note.pdf") {
        return 11;
    }
#if defined(__APPLE__)
    // 脚本行生成：命令动词、预填名、初始目录；缺省项不出现
    if (appleSaveFileDialogScript(SaveFileDialogOptions{}).front().find("choose file name") ==
        std::string::npos) {
        return 12;
    }
    const std::vector<std::string> lines = appleSaveFileDialogScript(source);
    if (lines.front().find("保存记事") == std::string::npos ||
        lines.front().find("default name \"未命名.txt\"") == std::string::npos ||
        lines.front().find("default location POSIX file \"/tmp/有 空格目录\"") == std::string::npos) {
        return 13;
    }
    // 引号转义：路径里的双引号不能截断脚本
    SaveFileDialogOptions quoted;
    quoted.prompt = "sa\"ve\\x";
    if (appleSaveFileDialogScript(quoted).front().find("sa\\\"ve\\\\x") == std::string::npos) {
        return 14;
    }
#endif
    std::cout << "save file dialog pure logic passed\n";
    return 0;
}
