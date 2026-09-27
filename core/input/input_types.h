#pragma once

#include "core/render/render_types.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace core {

enum class CursorShape {
    Arrow,
    Hand
};

enum class InputKey : std::uint16_t {
    Unknown,
    Backspace, Tab, Enter, Escape, Space,
    Insert, Delete, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    PrintScreen, ScrollLock, Pause, CapsLock, NumLock,
    LeftShift, RightShift, LeftControl, RightControl,
    LeftAlt, RightAlt, LeftSuper, RightSuper, Menu,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Apostrophe, Comma, Minus, Period, Slash, Semicolon, Equal,
    LeftBracket, Backslash, RightBracket, GraveAccent,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal, NumpadDivide, NumpadMultiply, NumpadSubtract,
    NumpadAdd, NumpadEnter, NumpadEqual,
    Count
};

enum class KeyAction {
    Press,
    Repeat,
    Release
};

struct KeyModifiers {
    bool control = false;
    bool shift = false;
    bool alt = false;
    bool super = false;
    bool capsLock = false;
    bool numLock = false;

    bool shortcut() const {
#if defined(__APPLE__)
        return super;
#else
        return control;
#endif
    }
};

struct KeyEvent {
    InputKey key = InputKey::Unknown;
    KeyAction action = KeyAction::Press;
    KeyModifiers modifiers;
    int scanCode = 0;

    bool isDown() const {
        return action == KeyAction::Press || action == KeyAction::Repeat;
    }
};

struct TextInputEvent {
    std::string text;
    std::string pasteText;
    std::string compositionText;
    bool composing = false;
    bool compositionChanged = false;

    bool hasInput() const {
        return !text.empty() || !pasteText.empty() || compositionChanged || composing ||
               !compositionText.empty();
    }
};

enum class PointerButton : std::uint8_t {
    None,
    Left,
    Middle,
    Right,
    X1,
    X2
};

class PointerButtons {
public:
    constexpr PointerButtons() = default;
    constexpr PointerButtons(PointerButton button) : bits_(bit(button)) {}

    constexpr bool contains(PointerButton button) const {
        return button != PointerButton::None && (bits_ & bit(button)) != 0;
    }

    constexpr bool empty() const { return bits_ == 0; }

    void set(PointerButton button, bool down) {
        if (button == PointerButton::None) {
            return;
        }
        if (down) {
            bits_ |= bit(button);
        } else {
            bits_ &= ~bit(button);
        }
    }

    friend constexpr PointerButtons operator|(PointerButtons left, PointerButtons right) {
        return PointerButtons(left.bits_ | right.bits_);
    }

    friend constexpr bool operator==(PointerButtons left, PointerButtons right) {
        return left.bits_ == right.bits_;
    }

    friend constexpr bool operator!=(PointerButtons left, PointerButtons right) {
        return !(left == right);
    }

private:
    explicit constexpr PointerButtons(std::uint32_t bits) : bits_(bits) {}

    static constexpr std::uint32_t bit(PointerButton button) {
        return button == PointerButton::None
            ? 0u
            : 1u << (static_cast<std::uint32_t>(button) - 1u);
    }

    std::uint32_t bits_ = 0;
};

constexpr PointerButtons operator|(PointerButton left, PointerButton right) {
    return PointerButtons(left) | PointerButtons(right);
}

enum class PointerAction {
    Move,
    Press,
    Release,
    Cancel
};

/**
 * @brief 指针事件坐标所处的空间（U1 尺度契约第一版，纯增量 API）。
 *
 * - Logical：逻辑像素（DSL 布局单位）。
 * - Physical：物理像素（帧缓冲像素，窗口/显示器上报的原始坐标）。
 *
 * 换算关系：physical = logical * scale（scale 见 PointerEvent::scale）。
 */
enum class PointerSpace {
    Logical,
    Physical
};

struct PointerEvent {
    double x = 0.0;
    double y = 0.0;
    double deltaX = 0.0;
    double deltaY = 0.0;
    PointerAction action = PointerAction::Move;
    PointerButton button = PointerButton::None;
    PointerButtons buttons;
    KeyModifiers modifiers;

    // ---- U1 尺度契约第一版（纯增量；上面既有字段语义一律不变）----
    // scale：本事件所属窗口的当前内容缩放系数（physical = logical * scale），
    //   即 app::update 传入的 dpiScale * app::uiScale()，与 compose 侧
    //   Screen::scale、app::contentScale() 同源同值。
    // space：本事件 x/y/deltaX/deltaY 所处的空间。**既有回调语义不变**——
    //   onPress/onRelease/onDrag 收到的坐标仍是 Physical（原始物理像素），
    //   onMove/onContextMenu 收到的仍是 Logical（框架已除过 scale）。
    // 统一换算（任意指针回调通用）：
    //   logical = (space == PointerSpace::Logical) ? v : v / scale
    //   physical = (space == PointerSpace::Physical) ? v : v * scale
    // 第二版会把全部指针回调统一到同一空间；届时只更新本注释，字段语义不变。
    float scale = 1.0f;
    PointerSpace space = PointerSpace::Physical;

    bool isDown(PointerButton value) const { return buttons.contains(value); }
    bool isPress(PointerButton value) const {
        return action == PointerAction::Press && button == value;
    }
    bool isRelease(PointerButton value) const {
        return action == PointerAction::Release && button == value;
    }
};

struct ScrollEvent {
    double x = 0.0;
    double y = 0.0;

    bool active() const { return x != 0.0 || y != 0.0; }
};

struct InteractionState {
    bool hover = false;
    bool pressed = false;
    bool clicked = false;
    bool pressStarted = false;
    bool released = false;
    bool canceled = false;
    bool drag = false;
    // 本次交互期间 drag 是否激活过（位移超阈值）。激活后即使松手回到元素内也不算 click。
    bool dragMoved = false;
    bool active = false;
    bool changed = false;
    PointerButton activeButton = PointerButton::None;
    double dragStartX = 0.0;
    double dragStartY = 0.0;
    double dragDeltaX = 0.0;
    double dragDeltaY = 0.0;

    void update(const Rect& bounds,
                const PointerEvent& event,
                bool topmostHover,
                PointerButtons acceptedButtons,
                double dragThreshold = 2.0,
                bool enabled = true) {
        const bool oldHover = hover;
        const bool oldPressed = pressed;
        const bool oldDrag = drag;
        const bool oldActive = active;
        const PointerButton oldActiveButton = activeButton;

        clicked = false;
        pressStarted = false;
        released = false;
        canceled = false;

        if (!enabled) {
            hover = false;
            pressed = false;
            drag = false;
            dragMoved = false;
            active = false;
            activeButton = PointerButton::None;
            dragDeltaX = 0.0;
            dragDeltaY = 0.0;
            changed = oldHover != hover || oldPressed != pressed || oldDrag != drag ||
                      oldActive != active || oldActiveButton != activeButton;
            return;
        }

        hover = topmostHover && bounds.contains(event.x, event.y);
        if (!active && hover && event.action == PointerAction::Press &&
            acceptedButtons.contains(event.button)) {
            active = true;
            activeButton = event.button;
            pressStarted = true;
            dragMoved = false;
            dragStartX = event.x;
            dragStartY = event.y;
        }

        pressed = active && event.buttons.contains(activeButton);
        dragDeltaX = event.x - dragStartX;
        dragDeltaY = event.y - dragStartY;
        drag = pressed && (std::fabs(dragDeltaX) > dragThreshold ||
                           std::fabs(dragDeltaY) > dragThreshold);
        if (drag) {
            dragMoved = true;
        }

        const bool matchingEnd = active && event.button == activeButton &&
            (event.action == PointerAction::Release || event.action == PointerAction::Cancel);
        if (matchingEnd) {
            released = true;
            canceled = event.action == PointerAction::Cancel;
            // drag 激活过的交互即使松手在元素内也不触发 click（拖拽不等于点击）。
            clicked = !canceled && !dragMoved && hover;
            active = false;
            pressed = false;
            drag = false;
            dragMoved = false;
            activeButton = PointerButton::None;
        }

        changed = oldHover != hover || oldPressed != pressed || oldDrag != drag ||
                  oldActive != active || oldActiveButton != activeButton || pressStarted ||
                  released || clicked || canceled;
    }
};

} // namespace core
