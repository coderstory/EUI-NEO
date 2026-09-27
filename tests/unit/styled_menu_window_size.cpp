// tests/unit/styled_menu_window_size.cpp
// styledMenu 窗口尺寸换算（显示器缩放）纯逻辑单测：
//   1. scale = 1.0 → 与旧公式（面板 + margin×2）逐像素一致（向后兼容）
//   2. scale = 1.25 → 结果 ×1.25（原缺陷：窗高等于逻辑高，125% 显示器上
//      菜单内容比窗大一个缩放比 → 末行文字溢出到窗底外、长菜单项被截）
//   3. 非整数像素向上取整（窗必须装得下内容，不能比内容小）
//   4. scale <= 0（查询异常）按 1:1 降级，不出 NaN/负尺寸
//   5. 与 styledMenuPanelHeight 组合：真实行表（3 行 + 1 分隔线）在 1.25 下
//      的窗高换算后仍 ≥ 面板逻辑高（内容不溢出）
//   6. windowScaleForPoint 平台查询守卫（无头可调、返回值 > 0）
// 真机 125% 显示器上的最终视觉验收（菜单末行不越底边）属人工验证范畴。
#include "components/styledmenu.h"
#include "core/window/window_backend.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-3f;
}

components::StyledMenuItem item(const char* text) {
    return components::StyledMenuItem{text};
}

} // namespace

int main() {
    using components::StyledMenuStyle;
    using components::styledMenuPanelHeight;
    using components::styledMenuWindowSize;

    const StyledMenuStyle style;  // 默认 token：rowHeight 34 / separator 9 /
                                  // inset 6 / margin 12
    check(near(style.rowHeight, 34.0f) && near(style.separatorHeight, 9.0f) &&
              near(style.inset, 6.0f) && near(style.margin, 12.0f),
          "default style tokens unchanged (34/9/6/12)");

    // ---- 1. scale = 1.0：与旧公式一致（面板 + margin×2）----
    const auto at1 = styledMenuWindowSize(220.0f, 200.0f, style.margin, 1.0f);
    check(near(at1.width, 244.0f), "scale 1.0: width == panelWidth + margin*2");
    check(near(at1.height, 224.0f), "scale 1.0: height == panelHeight + margin*2");

    // ---- 2. scale = 1.25：整份 ×1.25（125% 显示器）----
    const auto at125 = styledMenuWindowSize(220.0f, 200.0f, style.margin, 1.25f);
    check(near(at125.width, 305.0f), "scale 1.25: width == 244 * 1.25");
    check(near(at125.height, 280.0f), "scale 1.25: height == 224 * 1.25");
    check(at125.height > at1.height, "scale 1.25: window taller than unscaled");

    // ---- 3. 非整数像素向上取整（窗不能小于内容）----
    const auto rounded = styledMenuWindowSize(220.5f, 100.0f, 12.0f, 1.0f);
    check(near(rounded.width, 245.0f), "fractional width rounds up (244.5 → 245)");
    check(near(rounded.height, 124.0f), "integer height passes through (124)");
    const auto rounded125 = styledMenuWindowSize(101.0f, 100.0f, 0.0f, 1.25f);
    check(near(rounded125.width, 127.0f), "1.25 rounding: 101 * 1.25 = 126.25 → 127");

    // ---- 4. scale <= 0 降级为 1:1（查询异常不放大、不出 NaN）----
    const auto zero = styledMenuWindowSize(220.0f, 200.0f, 12.0f, 0.0f);
    check(near(zero.width, 244.0f) && near(zero.height, 224.0f),
          "scale 0 degrades to 1.0");
    const auto negative = styledMenuWindowSize(220.0f, 200.0f, 12.0f, -2.0f);
    check(near(negative.width, 244.0f) && near(negative.height, 224.0f),
          "negative scale degrades to 1.0");

    // ---- 5. 与面板度量组合：3 行 + 1 分隔线，125% 下窗仍装得下内容 ----
    const std::vector<components::StyledMenuItem> items{
        item("打开"), item("换肤"), item("-"), item("退出")};
    const std::vector<components::StyledMenuRow> rows = components::styledMenuFlatten(items);
    const float panelHeight = styledMenuPanelHeight(rows, style);  // 12 + 3*34 + 9
    check(near(panelHeight, 123.0f), "panel height: inset*2 + 3 rows + 1 separator");
    const float scale = 1.25f;
    const auto menu = styledMenuWindowSize(style.panelWidth, panelHeight, style.margin, scale);
    check(near(menu.height, 184.0f), "125%: window height = ceil((123 + 24) * 1.25)");
    // 折算回逻辑单位后窗高必须 ≥ 面板高（余量恰为 margin×2 阴影留白）
    check(menu.height / scale >= panelHeight,
          "125%: window content box covers panel height");

    // ---- 6. 平台查询守卫：无头可调，返回正系数 ----
    // 取值本身取决于进程 DPI 感知级别（GLFW 在 glfwInit 里设 per-monitor v2；
    // 本测试进程不设 → 未感知时系统按 96 虚拟化，真机 125% 下实测应用侧
    // 得 1.25），故只断言「可调用 + 正系数」，不断言具体倍数。
    const float pointScale = core::window::windowScaleForPoint(0.0f, 0.0f);
    check(pointScale > 0.0f, "windowScaleForPoint returns positive scale");
    check(core::window::windowScaleForPoint(-100.0f, -100.0f) > 0.0f,
          "windowScaleForPoint tolerates off-screen points");

    if (g_failures != 0) {
        std::printf("styled_menu_window_size: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("styled_menu_window_size: all assertions passed\n");
    return 0;
}
