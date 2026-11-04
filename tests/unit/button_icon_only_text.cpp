#include "components/button.h"
#include <iostream>

// bug180（DevDesk 池）：icon-only 按钮不得渲染默认占位文案 "Button"。
// 契约：文案节点只在显式 .text() 过才构建（.icon() 不清已设文案，
// .text()/.icon() 任意链序都保持双内容）；无文案无图标时不落任何文本节点。

namespace {

// 文案节点断言走 .text.normal（按钮文案恒逐态双文本：core::Color 默认 alpha=1
// 使 textHover 判真，单 .text 节点不存在——与 runtime 语义实证一致）
int expectTextNode(core::dsl::Ui& ui, const std::string& id, bool present, int errCode) {
    auto* node = ui.find(id + ".text.normal");
    if (present && node == nullptr) {
        std::cerr << id << ": text node missing\n";
        return errCode;
    }
    if (!present && node != nullptr) {
        std::cerr << id << ": text node must not exist (default label leak)\n";
        return errCode;
    }
    return 0;
}

} // namespace

int main() {
    core::dsl::Ui ui;

    // icon-only：无 .text() → 只有图标节点，不渲染占位文案（回归主契约）
    ui.begin("root");
    components::button(ui, "b.icon.only").size(28.0f, 28.0f).icon(0xF067).build();
    // icon 后补 text（agent/tools 链序）：双内容保留
    components::button(ui, "b.icon.then.text").size(120.0f, 32.0f).icon(0xF1E6).text("工具").build();
    // text 前置 icon 后置（home/刷新 链序）：双内容保留
    components::button(ui, "b.text.then.icon").size(120.0f, 32.0f).text("刷新").icon(0xF021).build();
    // 纯 text：与旧版一致
    components::button(ui, "b.text.only").size(120.0f, 32.0f).text("保存").build();
    ui.end();
    ui.layout(400.0f, 300.0f);

    if (auto* icon = ui.find("b.icon.only.icon"); icon == nullptr) {
        std::cerr << "icon-only button lost its icon node\n";
        return 1;
    }
    if (int rc = expectTextNode(ui, "b.icon.only", false, 2); rc != 0) return rc;

    if (int rc = expectTextNode(ui, "b.icon.then.text", true, 3); rc != 0) return rc;
    if (int rc = expectTextNode(ui, "b.text.then.icon", true, 4); rc != 0) return rc;
    if (int rc = expectTextNode(ui, "b.text.only", true, 5); rc != 0) return rc;

    std::cout << "button_icon_only_text unit tests passed\n";
    return 0;
}
