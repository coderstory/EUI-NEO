#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/window/window_backend.h"

namespace core::platform {

/**
 * @brief 自定义托盘菜单项。text 为 "-" 表示分隔线（callback 忽略）。
 *
 * 回调在主线程托盘事件轮询中触发（与 Show/Exit 默认项同一通路）。
 */
struct TrayMenuItem {
    std::string text;
    std::function<void()> callback;

    bool isSeparator() const { return text == "-"; }
};

struct TrayOptions {
    std::string tooltip;
    std::string iconPath;
    /** 自定义菜单项；为空时回退默认 Show/Exit。 */
    std::vector<TrayMenuItem> menuItems;
    /** 有自定义项时是否保留默认 Show/Exit（默认保留）。 */
    bool keepDefaultMenuItems = true;
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
/**
 * @brief 注册/更新自定义托盘菜单（托盘未初始化时先存储，随 initializeTray 生效）。
 *
 * items 为空回退默认 Show/Exit；keepDefault=false 时仅有自定义项
 * （此时应用需自行提供退出入口）。主线程调用。
 */
void setTrayMenu(const std::vector<TrayMenuItem>& items, bool keepDefault = true);
/**
 * @brief 托盘菜单接管 handler（当前仅 Windows tray.h Win32 分支触发）。
 *
 * 托盘图标按下（左/右键）时先于原生菜单弹出调用 handler(x, y, leftButton)：
 * 返回 true = 上层已自行展示菜单（如 components::showStyledMenu 独立菜单窗），
 * 原生 TrackPopupMenu 被跳过；返回 false = 降级弹原生托盘菜单（保持既有行为）。
 * 坐标为光标物理像素（GetCursorPos）。在托盘消息泵（主线程）里调用，可在
 * handler 内安全走 app::openWindow / requestUpdate。传空 std::function 注销。
 * 其他平台不调用，注册无副作用。
 */
using TrayMenuRequestedHandler = std::function<bool(int x, int y, bool left_button)>;
void setTrayMenuRequestedHandler(TrayMenuRequestedHandler handler);
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
