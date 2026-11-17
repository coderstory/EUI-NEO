// 保存文件对话框人工验证程序（need265）。对话框需真人交互，fixtures 只编译不进 ctest，
// 不会拖挂自动化测试；验证项与预期结果记录在任务报告。
// EUI fixture 是 DSL 应用形态：入口 main 由框架 glfw_app_main.cpp 提供，本文件只提供
// dslAppConfig/compose；首帧弹一次保存对话框，结果打到 stdout 后请求退出。
// 建议人工核验：预填名出现、缺扩展名被补齐、覆盖同名文件弹系统确认、取消返回
// Cancelled、中文与含空格路径原样往返（运行命令见任务报告）。
#include "eui/app.h"
#include "eui/dsl_app.h"
#include "eui/platform.h"

#include <iostream>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("保存对话框人工验证")
        .pageId("save_file_dialog_manual")
        .windowSize(420, 200)
        .iconPath("");
    return config;
}

void compose(eui::Ui&, const eui::Screen&) {
    static bool asked = false;
    if (asked) {
        return;
    }
    asked = true;

    eui::platform::SaveFileDialogOptions options;
    options.prompt = "保存记事（人工验证）";
    options.defaultName = "会议纪要.txt";
    options.defaultExtension = "txt";
    options.allowedExtensions = {"txt", "md"};
    options.initialDirectory = "/tmp";
    options.filterName = "文本文件";
    const eui::platform::FileDialogResult result = eui::platform::saveFileDialog(options);
    switch (result.status) {
        case eui::platform::FileDialogStatus::Selected:
            std::cout << "status=Selected";
            for (const std::string& path : result.paths) {
                std::cout << " path=" << path;
            }
            std::cout << "\n";
            break;
        case eui::platform::FileDialogStatus::Cancelled:
            std::cout << "status=Cancelled\n";
            break;
        case eui::platform::FileDialogStatus::Failed:
            std::cout << "status=Failed error=" << result.error << "\n";
            break;
    }
    app::requestExit();
}

} // namespace app
