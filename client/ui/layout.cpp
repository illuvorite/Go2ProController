#include "layout.hpp"

// ============================================================================
// 断点布局：这里只放**非 constexpr** 的名字函数。
//
// makeLayout / computeFloatingJoysticks 是 constexpr（隐含 inline），定义必须对所有
// 使用它的 TU 可见，所以实现在 layout.hpp 里（header-only）。
// 曾经把定义放在这个 .cpp 里 → ui.cpp / main_android.cpp 发出 -Wundefined-inline，
// 链接时报 undefined symbol: go2::makeLayout。改回来之前请先想清楚这一点。
// ============================================================================

namespace go2 {

const char* widthClassName(WidthClass c) {
    switch (c) {
        case WidthClass::Compact: return "C <600dp";
        case WidthClass::Medium: return "M 600–899dp";
        case WidthClass::Expanded: return "E 900–1279dp";
        case WidthClass::Large: return "L >=1280dp";
    }
    return "?";
}

const char* heightClassName(HeightClass c) {
    switch (c) {
        case HeightClass::Short: return "S <480dp";
        case HeightClass::Regular: return "R 480–799dp";
        case HeightClass::Tall: return "T >=800dp";
    }
    return "?";
}

}  // namespace go2
