// tests/unit/ui_set_scroll_offset.cpp
//
// 需求来源：DevDesk M6 滚动跟随（DevDesk/docs/superpowers/specs/
// 2026-09-28-setscrolloffset-upstream-m6-refactor-design.md §2.1 / §4.1）。
// Ui::setScrollOffset 的 Runtime-driven 单测：`core::dsl::Runtime rt;
// rt.compose(...)` 逐帧驱动，断言走 scrollView.onChange 回调收到的 v——
// 不经私有 instance.offset（审计 BLOCKER 1：Runtime::instances_ 无 accessor，
// onChange 收到的 v = setScrollOffset 钳位后的 instance.offset 等价断言）。
//
// 验证点（对应 spec §4.1）：
//  1) compose 帧末消费：帧内 setScrollOffset → 帧末 onChange v == 目标 offset
//  2) 同帧同 id 连续 set 两次（不同 offset）→ 只触发一次 onChange，v == 第二次
//     （per-id 去重，审计 major + perf minor）
//  3) pending 消费后清空（跨帧无残留；含 onChange 重入 setScrollOffset 的
//     take-and-clear 保护——审计 major 的迭代器失效/无限增长防护）
//  4) 本帧元素树无该 id → 请求丢弃（不排队跨帧；其他合法请求不连坐）
//  5) init-only 语义不被破坏：未调 setScrollOffset 的 scrollView，DSL
//     `.offset()` 仍只首帧生效（回归）

#include "components/scrollview.h"
#include "core/dsl.h"
#include "core/dsl_runtime.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>

namespace {

using Ui = core::dsl::Ui;
using Screen = core::dsl::Screen;
using Element = core::dsl::Element;

constexpr const char* kPage = "scrolltest";
constexpr const char* kScrollId = "sv";
constexpr float kViewport = 300.0f;   // scrollView 视口高
constexpr float kItemHeight = 200.0f; // 内容项高（3 项 + gap 4×2 → 内容高 608）

int g_failures = 0;

void checkImpl(bool ok, const char* expr, int line, const char* what) {
    if (!ok) {
        ++g_failures;
        std::cerr << "  FAIL line " << line << ": " << expr << " -- " << what << "\n";
    }
}

#define CHECK(cond) checkImpl(static_cast<bool>(cond), #cond, __LINE__, "")
#define CHECK_MSG(cond, what) checkImpl(static_cast<bool>(cond), #cond, __LINE__, what)

bool near(float left, float right, float tolerance = 0.5f) {
    return std::fabs(left - right) <= tolerance;
}

// 帧驱动环境：每帧 compose 重建 scrollView（与 DevDesk M6 同形接线），
// onChange 只记账（触发次数 + 末值），不经任何私有字段。
struct Env {
    core::dsl::Runtime rt;
    int changedCount = 0;
    float lastValue = 0.0f;
    float maxOffset = 0.0f;  // 帧内从 sv->scrollMaxOffset 捕获（元素指针帧内有效）

    void buildScrollView(Ui& ui, float dslOffset) {
        components::scrollView(ui, kScrollId)
            .position(0.0f, 0.0f)
            .size(600.0f, kViewport)
            .offset(dslOffset)
            .gap(4.0f)
            .onChange([this](float v) {
                ++changedCount;
                lastValue = v;
            })
            .content([&](Ui& u, float cw, float) {
                for (int i = 0; i < 3; ++i) {
                    u.rect(std::string(kScrollId) + ".item" + std::to_string(i))
                        .size(cw, kItemHeight)
                        .build();
                }
            })
            .build();
        if (const Element* sv = ui.find(kScrollId)) {
            maxOffset = sv->scrollMaxOffset;
        }
    }

    void frame(int /*reserved*/, bool withScrollView, float dslOffset,
               const std::function<void(Ui&, const Screen&)>& extra) {
        rt.compose(kPage, 1600.0f, 1000.0f, [&](Ui& ui, const Screen& screen) {
            if (withScrollView) {
                buildScrollView(ui, dslOffset);
            } else {
                ui.rect("other").size(100.0f, 100.0f).build();
            }
            if (extra) {
                extra(ui, screen);
            }
        });
    }

    void frameWithScroll(float dslOffset = 0.0f) {
        frame(0, true, dslOffset, {});
    }

    // 帧内调用一次 setScrollOffset（帧末被消费）
    void frameSet(float target) {
        frame(0, true, 0.0f, [&](Ui& ui, const Screen&) {
            ui.setScrollOffset(kScrollId, target);
        });
    }
};

bool testBasicConsumeAtFrameEnd() {
    Env env;
    // 帧 1：仅 DSL .offset(0) 初始化 → init 不触发 onChange
    env.frameWithScroll(0.0f);
    CHECK(env.changedCount == 0);
    // 帧 2：compose 回调内 setScrollOffset → 帧末消费 → onChange v == 目标
    env.frameSet(120.0f);
    CHECK_MSG(env.changedCount == 1, "帧末消费应恰好触发一次 onChange");
    CHECK(near(env.lastValue, 120.0f));
    CHECK(env.maxOffset > 120.0f);  // 内容 608 - 视口 300 → 未顶到 maxOffset，v 原样
    // 帧 3：无新请求的静默帧 → 不再触发
    env.frameWithScroll(0.0f);
    CHECK(env.changedCount == 1);
    return g_failures == 0;
}

bool testClampedToMaxOffset() {
    Env env;
    env.frameWithScroll(0.0f);
    // 越界 offset → 钳到 maxOffset（等价断言：onChange 收到钳位后值）
    env.frameSet(99999.0f);
    CHECK_MSG(env.changedCount == 1, "越界请求也只触发一次 onChange");
    CHECK(near(env.lastValue, env.maxOffset));
    CHECK(env.maxOffset > 120.0f);
    return g_failures == 0;
}

bool testSameFrameSameIdDedup() {
    Env env;
    env.frameWithScroll(0.0f);
    // 同帧同 id 连续两次（30 → 120）：per-id 去重 → 一次 onChange、v == 第二次
    env.frame(0, true, 0.0f, [&](Ui& ui, const Screen&) {
        ui.setScrollOffset(kScrollId, 30.0f);
        ui.setScrollOffset(kScrollId, 120.0f);
    });
    CHECK_MSG(env.changedCount == 1, "同帧同 id 两次 set 应只触发一次 onChange");
    CHECK(near(env.lastValue, 120.0f));
    return g_failures == 0;
}

bool testPendingClearedAcrossFrames() {
    Env env;
    env.frameWithScroll(0.0f);
    // 帧 2：set 30 → 消费即清空
    env.frameSet(30.0f);
    CHECK(env.changedCount == 1);
    CHECK(near(env.lastValue, 30.0f));
    // 帧 3：静默帧 → 队列已空，不再触发
    env.frameWithScroll(0.0f);
    CHECK(env.changedCount == 1);
    // 帧 4：新值 50 逐帧取件 → 不被旧条目污染
    env.frameSet(50.0f);
    CHECK(env.changedCount == 2);
    CHECK(near(env.lastValue, 50.0f));
    return g_failures == 0;
}

bool testReentrantSetFromOnChange() {
    Env env;
    bool reenterUsed = false;
    auto buildWithCallback = [&](Ui& ui) {
        components::scrollView(ui, kScrollId)
            .position(0.0f, 0.0f)
            .size(600.0f, kViewport)
            .offset(0.0f)
            .gap(4.0f)
            .onChange([&](float v) {
                ++env.changedCount;
                env.lastValue = v;
                if (!reenterUsed) {
                    reenterUsed = true;
                    // 帧末消费进行中重入：take-and-clear 后写进的是已清空的
                    // 成员 → 下一帧才消费；若消费直接遍历挂起成员则迭代器
                    // 失效 UB / 队列无限增长。
                    ui.setScrollOffset(kScrollId, 25.0f);
                }
            })
            .content([&](Ui& u, float cw, float) {
                for (int i = 0; i < 3; ++i) {
                    u.rect(std::string(kScrollId) + ".item" + std::to_string(i))
                        .size(cw, kItemHeight)
                        .build();
                }
            })
            .build();
    };

    // 帧 1：重入发生在帧末消费 onScrollOffsetChanged(120) 回调内
    env.rt.compose(kPage, 1600.0f, 1000.0f, [&](Ui& ui, const Screen&) {
        buildWithCallback(ui);
        ui.setScrollOffset(kScrollId, 120.0f);
    });
    CHECK_MSG(env.changedCount == 1, "帧末消费中重入不应在同帧再次消费");
    CHECK(near(env.lastValue, 120.0f));
    // 帧 2：消费重入挂起的 25
    env.rt.compose(kPage, 1600.0f, 1000.0f, [&](Ui& ui, const Screen&) {
        buildWithCallback(ui);
    });
    CHECK(env.changedCount == 2);
    CHECK(near(env.lastValue, 25.0f));
    // 帧 3：无新请求 → 链已断，不再触发
    env.rt.compose(kPage, 1600.0f, 1000.0f, [&](Ui& ui, const Screen&) {
        buildWithCallback(ui);
    });
    CHECK(env.changedCount == 2);
    return g_failures == 0;
}

bool testAbsentIdDropped() {
    Env env;
    // 帧 1：本帧树里没有 "sv" → 请求丢弃（不排队跨帧）
    env.frame(0, false, 0.0f, [&](Ui& ui, const Screen&) {
        ui.setScrollOffset(kScrollId, 60.0f);
    });
    // 帧 2：sv 就位（DSL offset 0 初始化）→ 请求没有排队 → onChange 零次
    env.frameWithScroll(0.0f);
    CHECK_MSG(env.changedCount == 0, "本帧无该元素 → 请求应丢弃，不排队跨帧");
    // 帧 3：同帧合法请求正常生效（丢弃只影响缺 id 那条，不连坐整队列）
    env.frameSet(60.0f);
    CHECK(env.changedCount == 1);
    CHECK(near(env.lastValue, 60.0f));
    return g_failures == 0;
}

bool testInitOnlySemanticsPreserved() {
    Env env;
    // 帧 1：DSL .offset(50) → 首帧 init 采纳 → instance 50（init 不触发 onChange）
    env.frameWithScroll(50.0f);
    CHECK(env.changedCount == 0);
    // 帧 2：DSL .offset(0) → init-only 语义：已初始化 → 不覆盖 instance（保持 50）
    env.frameWithScroll(0.0f);
    CHECK(env.changedCount == 0);
    // 帧 3：setScrollOffset(50) → 与 instance 相同 → closeEnough 早返回 → 不触发。
    // 若 init-only 被破坏（帧 2 把 instance 覆盖成 0），这里会假阳性触发。
    env.frameSet(50.0f);
    CHECK_MSG(env.changedCount == 0, "init-only 语义回归：已初始化后 DSL 值不覆盖 instance");
    // 帧 4：不同值如实送达
    env.frameSet(80.0f);
    CHECK(env.changedCount == 1);
    CHECK(near(env.lastValue, 80.0f));
    return g_failures == 0;
}

} // namespace

int main() {
    const struct {
        const char* name;
        bool (*fn)();
    } kTests[] = {
        {"consume_at_frame_end", testBasicConsumeAtFrameEnd},
        {"clamped_to_max_offset", testClampedToMaxOffset},
        {"same_frame_same_id_dedup", testSameFrameSameIdDedup},
        {"pending_cleared_across_frames", testPendingClearedAcrossFrames},
        {"reentrant_set_from_onchange", testReentrantSetFromOnChange},
        {"absent_id_dropped", testAbsentIdDropped},
        {"init_only_semantics_preserved", testInitOnlySemanticsPreserved},
    };
    int passed = 0;
    for (const auto& test : kTests) {
        const int failuresBefore = g_failures;
        test.fn();
        const bool ok = (g_failures == failuresBefore);
        const int failed = g_failures - failuresBefore;
        std::cout << (ok ? "PASS " : "FAIL ") << test.name << " (" << failed << " checks failed)\n";
        if (ok) {
            ++passed;
        }
    }
    std::cout << "ui_set_scroll_offset: " << passed << "/" << sizeof(kTests) / sizeof(kTests[0])
              << " passed\n";
    return g_failures == 0 ? 0 : 1;
}