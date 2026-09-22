#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/window/window_backend.h"

namespace core::platform {

struct TrayOptions {
    std::string tooltip;
    std::string iconPath;
};

struct FileDialogOptions {
    std::string prompt;
    std::vector<std::string> allowedExtensions;
    std::string initialDirectory;
    std::string filterName;
    bool allowMultiple = false;
};

enum class FileDialogStatus {
    Selected,
    Cancelled,
    Failed
};

struct FileDialogResult {
    FileDialogStatus status = FileDialogStatus::Cancelled;
    std::vector<std::string> paths;
    std::string error;

    bool selected() const {
        return status == FileDialogStatus::Selected && !paths.empty();
    }
};

bool repairCurrentWorkingDirectory();
std::string resolveResourcePath(const std::string& path);
bool openUrl(const std::string& url);
FileDialogResult openFileDialog(const FileDialogOptions& options = {});
std::string chooseFile(const FileDialogOptions& options = {});
std::vector<std::string> chooseFiles(const FileDialogOptions& options = {});
bool initializeTray(const TrayOptions& options);
bool isTrayInitialized();
void pollTray(bool blocking = false);
bool consumeTrayShowRequested();
bool consumeTrayExitRequested();
void shutdownTray();

/**
 * @brief 打开文件请求回调（Finder 双击 / 右键「打开方式」/ 拖到 Dock 图标）。
 *
 * 路径已做本地化处理（file URL -> POSIX 路径），在调用 pollOpenFiles() 的线程上回调。
 */
using OpenFileHandler = std::function<void(const std::string& path)>;

/**
 * @brief 安装原生「打开文件」事件处理器（macOS: kAEOpenDocuments Apple Event）。
 *
 * 幂等；必须在事件循环开始前从主线程调用，否则启动期的打开请求会被丢弃。
 * 非 macOS 平台为空操作。
 */
void installOpenFileHandler();

/**
 * @brief 注册唯一的打开文件回调（传空的 function 表示解绑）。
 */
void setOpenFileHandler(OpenFileHandler handler);

/**
 * @brief 排空待处理的打开文件请求并依次回调（主线程调用，见 app::initialize 与各 app main）。
 * @return 本次派发的路径数量。
 */
int pollOpenFiles();

void setImeCursorRect(window::Handle window, float x, float y, float width, float height);
void requestFrame();
void requestUiUpdate();
bool consumeUiUpdate();
bool consumeFrameRequest();

} // namespace core::platform
