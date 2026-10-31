// tests/unit/hit_test_interactive_at.cpp
//
// Runtime::hitTestInteractiveAt（公开只读命中查询，测试/驱动设施用）单测。
// 需求来源：DevDesk 债-18 UI 自动化驱动入口——click 步骤命中回读需要框架内
// 等价 Playwright hit-target check 的只读查询（不经 resolveHoverTarget 的
// hover 缓存，无副作用）。
// 四分支钉住语义：
//  1) 命中：命中点落在已知可交互元素返回其 id（含 dpiScale 换算正确性）
//  2) clip 外：点在元素 frame 内但在祖先 clip 区域外 → miss（空 id）
//  3) disabled 树：自身 disabled 或祖先 disabled → miss（空 id）
//  4) z 序顶层优先：两元素重叠时返回 zIndex 更高者（orderedRoots/orderedChildren
//     逆序遍历核心语义），不以 DOM 先后为准
// 返回 id 为 resolveId 解析后的全量 id（带页面前缀），期望值经 Ui::find(...)->id 捕获（Element.id 为解析后全量 id）。

#include "core/dsl.h"
#include "core/dsl_runtime.h"

#include <iostream>
#include <string>

namespace {

using Ui = core::dsl::Ui;
using Screen = core::dsl::Screen;

int g_failures = 0;

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
    return condition;
}

} // namespace

int main() {
    // 场景 1：命中 + 空命中 + dpiScale 换算
    {
        core::dsl::Runtime rt;
        std::string targetId;
        rt.compose("hit", 800.0f, 600.0f, [&](Ui& ui, const Screen&) {
            ui.stack("hit.target")
                .position(100.0f, 100.0f)
                .size(200.0f, 200.0f)
                .interactive(true)
                .build();
            targetId = ui.find("hit.target")->id;
        });

        check(rt.hitTestInteractiveAt(200.0, 200.0, 1.0f) == targetId,
              "point inside target center returns its id");
        check(rt.hitTestInteractiveAt(700.0, 500.0, 1.0f).empty(),
              "point on empty space returns empty id");
        check(rt.hitTestInteractiveAt(50.0, 50.0, 1.0f).empty(),
              "point outside target frame returns empty id");

        // dpiScale=2：逻辑点按 physical = logical * dpiScale 换算后再命中
        core::dsl::Runtime rtScaled;
        std::string scaledTargetId;
        rtScaled.compose("hitscale", 800.0f, 600.0f, [&](Ui& ui, const Screen&) {
            ui.stack("hitscale.target")
                .position(100.0f, 100.0f)
                .size(200.0f, 200.0f)
                .interactive(true)
                .build();
            scaledTargetId = ui.find("hitscale.target")->id;
        });
        check(rtScaled.hitTestInteractiveAt(100.0, 100.0, 2.0f) == scaledTargetId,
              "logical point at dpiScale=2 converts to physical hit");
        check(rtScaled.hitTestInteractiveAt(50.0, 50.0, 2.0f).empty(),
              "logical point outside at dpiScale=2 misses (physical 100,100 < frame 200,200)");
    }

    // 场景 2：clip 外 miss——clip.target 的 frame 一半在 clip.wrap 视口外
    {
        core::dsl::Runtime rt;
        std::string targetId;
        rt.compose("clip", 800.0f, 600.0f, [&](Ui& ui, const Screen&) {
            ui.stack("clip.wrap")
                .position(100.0f, 100.0f)
                .size(100.0f, 100.0f)
                .clip(true)
                .content([&] {
                    ui.stack("clip.target")
                        .position(50.0f, 50.0f)
                        .size(100.0f, 100.0f)
                        .interactive(true)
                        .build();
                })
                .build();
            targetId = ui.find("clip.target")->id;
        });

        check(rt.hitTestInteractiveAt(180.0, 180.0, 1.0f) == targetId,
              "point inside both frame and clip region hits");
        check(rt.hitTestInteractiveAt(230.0, 230.0, 1.0f).empty(),
              "point inside frame but outside clip region misses");
    }

    // 场景 3：disabled——祖先 disabled 与自身 disabled 都 miss，enabled 对照命中
    {
        core::dsl::Runtime rt;
        std::string okId;
        rt.compose("dis", 800.0f, 600.0f, [&](Ui& ui, const Screen&) {
            ui.stack("dis.ancestor")
                .position(0.0f, 0.0f)
                .size(200.0f, 200.0f)
                .disabled(true)
                .content([&] {
                    ui.stack("dis.child")
                        .position(50.0f, 50.0f)
                        .size(100.0f, 100.0f)
                        .interactive(true)
                        .build();
                })
                .build();
            ui.stack("dis.self")
                .position(300.0f, 0.0f)
                .size(100.0f, 100.0f)
                .interactive(true)
                .disabled(true)
                .build();
            ui.stack("dis.ok")
                .position(300.0f, 200.0f)
                .size(100.0f, 100.0f)
                .interactive(true)
                .build();
            okId = ui.find("dis.ok")->id;
        });

        check(rt.hitTestInteractiveAt(100.0, 100.0, 1.0f).empty(),
              "point inside child of disabled ancestor misses");
        check(rt.hitTestInteractiveAt(350.0, 50.0, 1.0f).empty(),
              "point inside self-disabled element misses");
        check(rt.hitTestInteractiveAt(350.0, 250.0, 1.0f) == okId,
              "enabled sibling still hits (control)");
    }

    // 场景 4：z 序顶层优先——重叠点上 zIndex 高者赢（即使 DOM 在前）
    {
        core::dsl::Runtime rt;
        std::string highId, lowId;
        rt.compose("zorder", 800.0f, 600.0f, [&](Ui& ui, const Screen&) {
            ui.stack("z.high")
                .position(0.0f, 0.0f)
                .size(200.0f, 200.0f)
                .interactive(true)
                .zIndex(5)
                .build();
            ui.stack("z.low")
                .position(100.0f, 0.0f)
                .size(200.0f, 200.0f)
                .interactive(true)
                .build();
            highId = ui.find("z.high")->id;
            lowId = ui.find("z.low")->id;
        });

        check(rt.hitTestInteractiveAt(150.0, 100.0, 1.0f) == highId,
              "overlap point returns higher-z element though it is earlier in DOM");
        check(rt.hitTestInteractiveAt(250.0, 100.0, 1.0f) == lowId,
              "point only over low-z element returns it");
    }

    if (g_failures == 0) {
        std::cout << "hit_test_interactive_at: all checks passed\n";
    }
    return g_failures;
}
