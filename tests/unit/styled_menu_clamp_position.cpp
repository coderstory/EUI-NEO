// tests/unit/styled_menu_clamp_position.cpp
// styledMenu 托盘锚点（bug 135）纯逻辑单测：
//   规格（2026-11-05 老板终裁，平台各自语义；演进：2026-11-03 左下角锚）：
//   - Windows：弹窗**左上角** = 光标位置（原生 TrackPopupMenu 观感，向右下
//     展开），钳制走 flipUpOnBottomOverflow=true 翻转档（下缘不够 → 底贴
//     工作区底缘向上展开；右缘不够 → 贴右缘向左展开）；
//   - 其他平台（macOS）：维持弹窗**左下角** = 光标位置，钳制护左/右/顶
//     **不护底**（护底把菜单抬离光标 = 旧「往右上角迁移」症状成因）。
//   覆盖三处缺陷的纯函数面：
//   1. styledMenuClampPosition：不护底档（默认参，mac）+ 底缘翻转档（win）
//      的护左/护右/护顶语义；
//   2. styledMenuSelectWorkArea 容纳性选择：包含光标点直接命中；光标在任
//      务栏带（工作区外、+80px 容差档）也命中正确显示器；仍无命中取**最近**
//      工作区（点到矩形距离），不再取列表首个（旧实现取首个 = 多屏选错显
//      示器 =「往右上角迁移」的另一半成因）；
//   3. 无效工作区（w/h<=0）原样返回 / 选择返回空——无头降级语义不变。
// 纯函数直调（无平台依赖，headless 可跑）。真机托盘右键的最终视觉验收
// （win：菜单左上角贴光标/翻转观感；mac：左下角贴光标）属人工验证范畴。
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

core::window::MonitorWorkArea area(int x, int y, int width, int height) {
    core::window::MonitorWorkArea workArea;
    workArea.x = x;
    workArea.y = y;
    workArea.width = width;
    workArea.height = height;
    return workArea;
}

} // namespace

int main() {
    using components::StyledMenuRect;
    using components::styledMenuClampPosition;
    using components::styledMenuSelectWorkArea;

    // 主屏工作区 0,0 1920x1015（全屏 0..1080，任务栏带 1015..1080）；
    // 菜单窗 244x187（125% 档：220 面板 + margin×2 折算，同窗口尺寸单测口径）
    const float areaX = 0.0f;
    const float areaY = 0.0f;
    const float areaWidth = 1920.0f;
    const float areaHeight = 1015.0f;
    const float menuWidth = 244.0f;
    const float menuHeight = 187.0f;

    // ---- 1. 托盘锚点（缺陷③核心场景）：光标在任务栏带 y=1048，锚点折算
    //      desiredY = 1048 - 187 = 861 → 底边 = 1048 = 光标。**不护底**：
    //      y 原样放行（旧实现把底边抬到工作区下缘 1015 → 菜单离开光标 33px）
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            1200.0f, 1048.0f - menuHeight, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight);
        check(near(rect.y, 861.0f),
              "taskbar cursor: desired y passes through (no bottom clamp)");
        check(near(rect.y + rect.height, 1048.0f),
              "taskbar cursor: menu bottom edge stays at cursor y");
        check(near(rect.x, 1200.0f), "taskbar cursor: x passes through");
    }

    // ---- 2. 光标屏幕中部：锚点位完全在工作区内 → 原样 ----
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            800.0f, 500.0f - menuHeight, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight);
        check(near(rect.x, 800.0f) && near(rect.y, 500.0f - menuHeight),
              "mid-screen anchor: pass-through");
    }

    // ---- 3. 光标近右缘：护右贴右缘（x = right - 窗宽）----
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            1900.0f, 400.0f, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight);
        check(near(rect.x, areaWidth - menuWidth), "near right edge: clamp to right");
        check(near(rect.y, 400.0f), "near right edge: y untouched");
    }

    // ---- 4. 光标近左缘：护左抬回 areaX ----
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            -30.0f, 400.0f, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight);
        check(near(rect.x, areaX), "near left edge: clamp to left");
    }

    // ---- 5. 越上缘（超长菜单 / 屏顶光标）：护顶贴 areaY ----
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            100.0f, -50.0f, menuWidth, 600.0f,
            areaX, areaY, areaWidth, areaHeight);
        check(near(rect.y, areaY), "above top: clamp to top");
    }

    // ---- 5b. win 左上锚 + 翻转档（flipUpOnBottomOverflow=true，2026-11-05
    //      老板终裁）：期望位 = 光标本身（desiredY = 光标y），期望底越工作
    //      区底缘 → 底贴底缘（向上展开，对齐原生 TrackPopupMenu 任务栏观感）
    {
        // 光标在任务栏带 y=1048：左上锚 desiredY=1048 → 期望底 1048+187=1235
        // 越底缘 1015 → 翻转 y = 1015-187 = 828（菜单底 = 任务栏上缘）
        const StyledMenuRect rect = styledMenuClampPosition(
            1200.0f, 1048.0f, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight, true);
        check(near(rect.y, areaHeight - menuHeight),
              "win taskbar cursor: flip up, menu bottom sits on work-area bottom");
        check(near(rect.x, 1200.0f), "win taskbar cursor: x passes through");
    }
    // 托盘在屏幕右下角：右缘 + 底缘同时溢出 → 贴右 + 翻转上
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            1900.0f, 1048.0f, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight, true);
        check(near(rect.x, areaWidth - menuWidth),
              "win bottom-right tray: clamp to right edge");
        check(near(rect.y, areaHeight - menuHeight),
              "win bottom-right tray: flip up past bottom edge");
    }
    // 翻转档 + 工作区比菜单矮：翻转值 < areaY → 护顶兜底（保顶可见优先）
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            100.0f, 300.0f, menuWidth, 1200.0f,
            areaX, areaY, areaWidth, areaHeight, true);
        check(near(rect.y, areaY),
              "win flip with undersized work area: top clamp wins");
    }
    // 翻转档 + 不越底（光标屏幕中部）：原样放行（翻转不误伤正常锚定）
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            800.0f, 500.0f, menuWidth, menuHeight,
            areaX, areaY, areaWidth, areaHeight, true);
        check(near(rect.x, 800.0f) && near(rect.y, 500.0f),
              "win flip mid-screen: pass-through");
    }

    // ---- 6. 无效工作区（w/h<=0，无头/查询失败）：原样返回（降级语义不变）----
    {
        const StyledMenuRect rect = styledMenuClampPosition(
            10.0f, 20.0f, menuWidth, menuHeight, 0.0f, 0.0f, 0.0f, 0.0f);
        check(near(rect.x, 10.0f) && near(rect.y, 20.0f),
              "invalid work area: pass-through");
    }

    // ---- 7. 工作区选择：双屏（副屏在上、列表首个；主屏在下带任务栏）----
    //      旧缺陷 = 包含点判定必无命中（光标在任务栏带）→ 回退取列表首个
    //      = 错误显示器
    const std::vector<core::window::MonitorWorkArea> areas{
        area(0, -1080, 1920, 1080),  // 副屏（上方，无任务栏；列表首个）
        area(0, 0, 1920, 1015),      // 主屏（托盘所在）
    };
    check(styledMenuSelectWorkArea(areas, 800.0f, 500.0f) == &areas[1],
          "inside: pick containing area");
    // 光标在任务栏带（主屏工作区下缘外 33px，1080 任务栏带内，真机 bug 135
    // 实测坐标档）→ 容差档命中主屏，不落副屏
    check(styledMenuSelectWorkArea(areas, 1800.0f, 1048.0f) == &areas[1],
          "taskbar band: pick the monitor under the tray cursor");
    // 远离所有工作区（左上屏外）→ 最近者（副屏）胜出，而非列表首个
    check(styledMenuSelectWorkArea(areas, -500.0f, -1400.0f) == &areas[0],
          "off-screen: nearest area wins, not first in list");

    // ---- 8. 全无效/空表：nullptr（调用方原样放行）----
    const std::vector<core::window::MonitorWorkArea> invalid{area(0, 0, 0, 0)};
    check(styledMenuSelectWorkArea(invalid, 5.0f, 5.0f) == nullptr,
          "all-invalid areas: nullptr");
    check(styledMenuSelectWorkArea({}, 5.0f, 5.0f) == nullptr,
          "empty area list: nullptr");

    if (g_failures != 0) {
        std::printf("styled_menu_clamp_position: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("styled_menu_clamp_position: all assertions passed\n");
    return 0;
}
