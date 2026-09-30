// tests/unit/zindex_escape.cpp
// 子树 zIndex 上界逃逸：嵌套的高 zIndex 元素（dropdown/contextmenu 弹层）
// 应把所在子树在兄弟排序中抬到后续兄弟之上，而不是被困在父容器内部。
// 需求来源：DevDesk 设置页 scrollView 内卡片里的皮肤下拉弹层被后续卡片
// 遮挡（zIndex 此前只作用于同父兄弟，弹层无法跨越卡片边界）。
#include "core/dsl.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
    return condition;
}

// orderedChildren 里目标元素的下标（按指针比较；id 经 resolveId 带页面前缀）
int orderIndex(const core::dsl::Element* parent, const core::dsl::Element* child) {
    for (size_t i = 0; i < parent->orderedChildren.size(); ++i) {
        if (parent->orderedChildren[i] == child) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

int main() {
    using core::dsl::Ui;

    // 场景 1：弹层逃逸——card.a 内部嵌套 zIndex(20) 的弹层，
    // 后绘制的兄弟 card.b / card.c（zIndex 0）不得盖住它
    {
        Ui ui;
        ui.begin("escape");
        ui.stack("page")
            .size(800.0f, 600.0f)
            .content([&] {
                ui.stack("card.a")
                    .size(400.0f, 100.0f)
                    .content([&] {
                        ui.stack("card.a.wrap")
                            .size(400.0f, 100.0f)
                            .content([&] {
                                ui.stack("card.a.wrap.popup")
                                    .size(400.0f, 200.0f)
                                    .zIndex(20)
                                    .build();
                            })
                            .build();
                    })
                    .build();
                ui.stack("card.b").size(400.0f, 100.0f).build();
                ui.stack("card.c").size(400.0f, 100.0f).build();
            })
            .build();
        ui.end();
        ui.layout(800.0f, 600.0f);

        const auto* page = ui.find("page");
        if (!check(page != nullptr, "page element missing")) return g_failures;
        const auto* cardA = ui.find("card.a");
        const auto* cardB = ui.find("card.b");
        const auto* cardC = ui.find("card.c");
        if (!check(cardA && cardB && cardC, "cards missing")) return g_failures;
        check(cardA->subtreeMaxZIndex == 20,
              "card.a subtreeMaxZIndex should propagate nested popup z=20");
        const int idxA = orderIndex(page, cardA);
        const int idxB = orderIndex(page, cardB);
        const int idxC = orderIndex(page, cardC);
        check(idxA >= 0 && idxB >= 0 && idxC >= 0, "all cards present in orderedChildren");
        check(idxA > idxB && idxA > idxC,
              "popup-bearing card.a must sort after later siblings card.b/card.c");
    }

    // 场景 2：回归——全零 zIndex 时保持 DOM 顺序（稳定排序语义不变）
    {
        Ui ui;
        ui.begin("flat");
        ui.stack("page")
            .size(800.0f, 600.0f)
            .content([&] {
                ui.stack("first").size(400.0f, 100.0f).build();
                ui.stack("second").size(400.0f, 100.0f).build();
                ui.stack("third").size(400.0f, 100.0f).build();
            })
            .build();
        ui.end();
        ui.layout(800.0f, 600.0f);

        const auto* page = ui.find("page");
        if (!check(page != nullptr, "page element missing (flat)")) return g_failures;
        const auto* first = ui.find("first");
        const auto* second = ui.find("second");
        const auto* third = ui.find("third");
        check(orderIndex(page, first) == 0 && orderIndex(page, second) == 1 &&
                  orderIndex(page, third) == 2,
              "zero-z siblings keep DOM order");
    }

    // 场景 3：回归——平级自身 zIndex 排序语义不变（高 z 在后 = 绘制在上）
    {
        Ui ui;
        ui.begin("selfz");
        ui.stack("page")
            .size(800.0f, 600.0f)
            .content([&] {
                ui.stack("plain").size(400.0f, 100.0f).build();
                ui.stack("raised").size(400.0f, 100.0f).zIndex(5).build();
            })
            .build();
        ui.end();
        ui.layout(800.0f, 600.0f);

        const auto* page = ui.find("page");
        if (!check(page != nullptr, "page element missing (selfz)")) return g_failures;
        check(orderIndex(page, ui.find("raised")) > orderIndex(page, ui.find("plain")),
              "own zIndex still sorts above later plain sibling");
    }

    // 场景 4（等值 z 碰撞）：两个纵向相邻 dropdown（DevDesk settings 桌宠卡
    // 「皮肤」「大小」行，2026-09-30）。组件结构：wrap(z0) > dropdown 根
    // stack(z20) > popup。修复前 popup 无自档，两个 ddwrap 子树上界同为 20 →
    // 稳定排序保持 DOM 序 → 后绘 wrap.b 的字段盖住 wrap.a 展开的弹层。
    // 修复后 open 态 popup 自带 kPopupOpenZIndex（components/dropdown.h），
    // wrap.a 子树上界 900 > wrap.b 20 → wrap.a 排到其后，弹层绘制在最上。
    {
        Ui ui;
        ui.begin("equalz");
        ui.stack("page")
            .size(400.0f, 400.0f)
            .content([&] {
                ui.stack("wrap.a")
                    .size(200.0f, 40.0f)
                    .content([&] {
                        ui.stack("dd.a")
                            .size(200.0f, 40.0f)
                            .zIndex(20)
                            .content([&] {
                                ui.stack("dd.a.popup")
                                    .size(200.0f, 100.0f)
                                    .zIndex(900)  // open 态 kPopupOpenZIndex
                                    .build();
                            })
                            .build();
                    })
                    .build();
                ui.stack("wrap.b")
                    .size(200.0f, 40.0f)
                    .content([&] {
                        ui.stack("dd.b")
                            .size(200.0f, 40.0f)
                            .zIndex(20)
                            .build();
                    })
                    .build();
            })
            .build();
        ui.end();
        ui.layout(400.0f, 400.0f);

        const auto* page = ui.find("page");
        if (!check(page != nullptr, "page element missing (equalz)")) return g_failures;
        const auto* wrapA = ui.find("wrap.a");
        const auto* wrapB = ui.find("wrap.b");
        if (!check(wrapA && wrapB, "wraps missing (equalz)")) return g_failures;
        check(wrapA->subtreeMaxZIndex == 900,
              "open popup z=900 must lift wrap.a subtree (got " +
                  std::to_string(wrapA->subtreeMaxZIndex) + ")");
        const int idxA = orderIndex(page, wrapA);
        const int idxB = orderIndex(page, wrapB);
        check(idxA > idxB,
              "open-dropdown wrap.a must sort after equal-z later sibling wrap.b");
    }

    // 场景 5（场景 4 的关闭态回归）：popup 归 0 后 dd.a 子树上界回到字段档
    // 20，与 dd.b 等值 → DOM 序保持（修复不改变关闭态行为）
    {
        Ui ui;
        ui.begin("equalz-closed");
        ui.stack("page")
            .size(400.0f, 400.0f)
            .content([&] {
                ui.stack("wrap.a")
                    .size(200.0f, 40.0f)
                    .content([&] {
                        ui.stack("dd.a")
                            .size(200.0f, 40.0f)
                            .zIndex(20)
                            .content([&] {
                                ui.stack("dd.a.popup")
                                    .size(200.0f, 100.0f)
                                    .zIndex(0)  // 关闭态
                                    .build();
                            })
                            .build();
                    })
                    .build();
                ui.stack("wrap.b")
                    .size(200.0f, 40.0f)
                    .content([&] {
                        ui.stack("dd.b")
                            .size(200.0f, 40.0f)
                            .zIndex(20)
                            .build();
                    })
                    .build();
            })
            .build();
        ui.end();
        ui.layout(400.0f, 400.0f);

        const auto* page = ui.find("page");
        if (!check(page != nullptr, "page element missing (equalz-closed)")) return g_failures;
        const auto* wrapA = ui.find("wrap.a");
        if (!check(wrapA != nullptr, "wrap.a missing (equalz-closed)")) return g_failures;
        check(wrapA->subtreeMaxZIndex == 20,
              "closed popup must keep field-tier subtree bound 20 (got " +
                  std::to_string(wrapA->subtreeMaxZIndex) + ")");
        check(orderIndex(page, ui.find("wrap.a")) < orderIndex(page, ui.find("wrap.b")),
              "closed dropdowns keep DOM order (no behavior change)");
    }

    if (g_failures == 0) {
        std::cout << "zindex_escape: all checks passed\n";
    }
    return g_failures;
}
