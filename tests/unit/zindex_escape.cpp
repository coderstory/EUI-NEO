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

    if (g_failures == 0) {
        std::cout << "zindex_escape: all checks passed\n";
    }
    return g_failures;
}
