// tests/unit/styled_menu_wechat_style.cpp
// styledMenu 微信观感样式契约单测（真机 UAT「样式与微信不搭」整改的回归门）：
//   1. 注入侧按 DevDesk componentTokensWeChat 同款映射传 wechat 令牌时，
//      StyledMenuStyle 构造真正消费它们——面板 = surface（白底卡，不再按
//      dark 混黑洗成灰卡）、hover/pressed = surfaceActive（selectedBg，
//      旧 surfaceHover=dividerSecondary 过淡近不可见）、文字 = text
//      （textPrimary）、muted = text 0.67 alpha（textSecondary #666666 当量）、
//      separator/border 按注入 border（dividerPrimary）淡化派生；
//   2. dark=true（DevDesk 默认深色主题）时上述白卡/浅影契约不变——wechat
//      令牌深浅同值，阴影恒浅档（12 blur / 5 偏移），不随 dark 加重；
//   3. 几何档：radius 6（radiusWeChat 档）/ 行高 36 / inset 4 / rowPaddingX 12
//      （12-16 内边距档）/ margin 12；
//   4. 图标列判定（styledMenuAnyIcon）：任一行有图标 → true（文字跨行对齐的
//      前置），全无图标 → false（不空列）；
//   5. 面板高度按 36 行高换算（3 行 + 1 分隔线 = 8 + 108 + 9）。
// 视觉终验（真机观感）属人工 UAT 范畴；此处锁「注入令牌被消费」的机制层。
#include "components/styledmenu.h"

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

bool sameColor(const core::Color& actual, const core::Color& expected) {
    return near(actual.r, expected.r) && near(actual.g, expected.g) &&
           near(actual.b, expected.b) && near(actual.a, expected.a);
}

// DevDesk core::theme::componentTokensWeChat 的同款映射（wechat 令牌深浅同值，
// dark 位随宿主主题）：EUI 菜单只允许经这组 token 拿到微信观感——本测试锁住
// 「EUI 渲染真消费注入令牌」这条链。
components::theme::ThemeColorTokens wechatTokens(bool dark) {
    components::theme::ThemeColorTokens tokens;
    tokens.background = core::Color("#F7F7F7");   // wechat.pageBg
    tokens.primary = core::Color("#07C160");      // wechat.primary
    tokens.surface = core::Color("#FFFFFF");      // wechat.cardBg（深浅同值）
    tokens.surfaceHover = core::Color("#EDEDED"); // wechat.dividerSecondary
    tokens.surfaceActive = core::Color("#E5E5E5"); // wechat.selectedBg
    tokens.text = core::Color("#1A1A1A");         // wechat.textPrimary
    tokens.border = core::Color("#E5E5E5");       // wechat.dividerPrimary
    tokens.dark = dark;
    return tokens;
}

} // namespace

int main() {
    using components::StyledMenuStyle;
    const core::Color kSelectedBg = core::Color("#E5E5E5");
    const core::Color kTextPrimary = core::Color("#1A1A1A");

    // ---- 1/2. 令牌消费：浅色与深色主题同契约（wechat 档深浅同值）----
    for (int darkCase = 0; darkCase <= 1; ++darkCase) {
        const bool dark = darkCase == 1;
        const StyledMenuStyle style(wechatTokens(dark));

        check(sameColor(style.background, core::Color("#FFFFFF")),
              "panel background == injected surface (white card, no dark mix)");
        check(sameColor(style.hover, kSelectedBg),
              "hover row == injected surfaceActive (selectedBg)");
        check(sameColor(style.pressed, kSelectedBg), "pressed == surfaceActive");
        check(sameColor(style.text, kTextPrimary),
              "label text == injected text (textPrimary)");
        // muted = textPrimary RGB + 0.67 alpha（#666666 在白卡上的当量档）
        check(near(style.mutedText.r, kTextPrimary.r) &&
                  near(style.mutedText.g, kTextPrimary.g) &&
                  near(style.mutedText.b, kTextPrimary.b) &&
                  near(style.mutedText.a, 0.67f),
              "muted text = textPrimary at 0.67 alpha (textSecondary #666666 equivalent)");
        // separator/border 从注入 border（dividerPrimary）派生（半透明档）
        check(style.separator.a < 1.0f && style.border.a < 1.0f &&
                  style.separator.a > 0.0f,
              "separator/border derived from injected border with opacity");
        check(sameColor(style.accent, core::Color("#07C160")),
              "accent/check color == injected primary");
        // 阴影恒浅档（轻）：blur 12 / offsetY 5，不随 dark 加重
        check(style.shadow.enabled && near(style.shadow.blur, 12.0f) &&
                  near(style.shadow.offset.y, 5.0f) &&
                  near(style.shadow.color.a, 0.14f),
              "shadow stays light-tier (12/5) regardless of dark flag");
        // 几何档
        check(near(style.radius, 6.0f), "panel radius == 6 (radiusWeChat tier)");
        check(near(style.rowHeight, 36.0f), "row height ~36px");
        check(near(style.inset, 4.0f), "pill inset == 4");
        check(near(style.rowPaddingX, 12.0f), "row inner padding == 12 (12-16 band)");
        check(near(style.margin, 12.0f), "shadow margin == 12");
    }

    // ---- 3. dark=true 单独点名（DevDesk 默认主题下的关键回归：白卡不被
    //         混黑洗灰）----
    {
        const StyledMenuStyle style(wechatTokens(true));
        check(sameColor(style.background, core::Color("#FFFFFF")),
              "dark host theme keeps white card panel");
    }

    // ---- 4. 图标列判定：任一行有图标 → 整菜单保留图标列 ----
    {
        std::vector<components::StyledMenuItem> mixed;
        components::StyledMenuItem withIcon;
        withIcon.text = "有图标";
        withIcon.iconCodepoint = 0xF2D2;
        mixed.push_back(withIcon);
        components::StyledMenuItem noIcon;
        noIcon.text = "无图标";
        mixed.push_back(noIcon);
        const std::vector<components::StyledMenuRow> mixedRows =
            components::styledMenuFlattenAllExpanded(mixed);
        check(components::styledMenuAnyIcon(mixedRows),
              "anyIcon: one icon row -> icon column reserved");

        const std::vector<components::StyledMenuItem> textOnly{
            components::StyledMenuItem{"纯文本"}};
        check(!components::styledMenuAnyIcon(
                  components::styledMenuFlattenAllExpanded(textOnly)),
              "anyIcon: no icons -> no icon column");

        // 分隔线不算图标行
        const std::vector<components::StyledMenuItem> sepOnly{
            components::StyledMenuItem{"-"}};
        check(!components::styledMenuAnyIcon(
                  components::styledMenuFlattenAllExpanded(sepOnly)),
              "anyIcon: separator does not count as icon row");
    }

    // ---- 5. 面板高度按 36 行高：3 行 + 1 分隔线 ----
    {
        const StyledMenuStyle style(wechatTokens(false));
        const std::vector<components::StyledMenuItem> items{
            components::StyledMenuItem{"显示主窗口"},
            components::StyledMenuItem{"设置"},
            components::StyledMenuItem{"-"},
            components::StyledMenuItem{"退出"}};
        const std::vector<components::StyledMenuRow> rows =
            components::styledMenuFlatten(items);
        const float height = components::styledMenuPanelHeight(rows, style);
        check(near(height, 125.0f), "panel height = inset*2 + 3*36 + 9 = 125");
    }

    if (g_failures != 0) {
        std::printf("styled_menu_wechat_style: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("styled_menu_wechat_style: all assertions passed\n");
    return 0;
}
