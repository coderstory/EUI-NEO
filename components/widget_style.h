#pragma once

#include "components/theme.h"
#include "core/render/render_types.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace components {

// -- S0#4 控件样式条目 ----------------------------------------------------------
struct WidgetStyleEntry {
    core::Color bg;
    core::Color text;
    core::Color border;
    float radius = 0.0f;
    float fontSize = 0.0f;
    float height = 0.0f;
};

// -- S0#4 全局控件样式注册表（单例）---------------------------------------------
// 键 = "widgetType:variant:state"（如 "Button:Filled:default"）
// 组件构造时查询默认样式，未注册则用组件自身的 Builder 默认值
class WidgetStyleRegistry {
public:
    static WidgetStyleRegistry& instance() {
        static WidgetStyleRegistry registry;
        return registry;
    }

    void set(const std::string& widget, const std::string& variant,
             const std::string& state, const WidgetStyleEntry& entry) {
        entries_[key(widget, variant, state)] = entry;
    }

    std::optional<WidgetStyleEntry> query(const std::string& widget,
                                           const std::string& variant,
                                           const std::string& state) const {
        auto it = entries_.find(key(widget, variant, state));
        if (it != entries_.end()) return it->second;
        // fallback: 查 "widget:*:state"
        it = entries_.find(key(widget, "*", state));
        if (it != entries_.end()) return it->second;
        return std::nullopt;
    }

    void clear() { entries_.clear(); }

private:
    WidgetStyleRegistry() = default;

    static std::string key(const std::string& widget, const std::string& variant,
                           const std::string& state) {
        return widget + ":" + variant + ":" + state;
    }

    std::unordered_map<std::string, WidgetStyleEntry> entries_;
};

} // namespace components