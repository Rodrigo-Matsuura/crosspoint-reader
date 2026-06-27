#include "ThemeLayout.h"

#include <algorithm>
#include <cstdint>

#include <FreeInkUILayout.h>

int themeLayoutTokenSize(const ThemeMetrics& metrics, const std::string& token) {
  if (token == "topPadding") return metrics.topPadding;
  if (token == "header") return metrics.headerHeight;
  if (token == "tabBar" || token == "tabs") return metrics.tabBarHeight;
  if (token == "footer" || token == "buttons" || token == "buttonHints") return metrics.buttonHintsHeight;
  if (token == "row") return metrics.listRowHeight;
  if (token == "subtitleRow") return metrics.listWithSubtitleRowHeight;
  if (token == "menuRow") return metrics.menuRowHeight;
  if (token == "recents") return metrics.homeCoverTileHeight;
  if (token == "cover") return metrics.homeCoverHeight;
  if (token == "verticalSpacing" || token == "gap") return metrics.verticalSpacing;
  if (token == "progress") return metrics.progressBarHeight;
  return 0;
}

namespace {

freeink::ui::Rect toUiRect(Rect rect) {
  return freeink::ui::Rect{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                           static_cast<int16_t>(rect.height)};
}

Rect fromUiRect(freeink::ui::Rect rect) {
  return Rect{rect.x, rect.y, rect.width, rect.height};
}

freeink::ui::Axis toUiAxis(ThemeLayoutAxis axis) {
  return axis == ThemeLayoutAxis::Row ? freeink::ui::Axis::Row : freeink::ui::Axis::Column;
}

freeink::ui::LayoutLength toUiLength(const ThemeLayoutNode& node, const ThemeMetrics& metrics) {
  if (node.sizeType == ThemeLayoutSizeType::Fixed) {
    return freeink::ui::LayoutLength::fixed(static_cast<int16_t>(std::max(0, node.size)));
  }

  if (node.sizeType == ThemeLayoutSizeType::Token) {
    return freeink::ui::LayoutLength::fixed(
        static_cast<int16_t>(std::max(0, themeLayoutTokenSize(metrics, node.sizeToken))));
  }

  return freeink::ui::LayoutLength::flexible(static_cast<uint8_t>(std::max(1, node.flex)));
}

}  // namespace

void layoutThemeSlots(const ThemeLayoutNode& node, Rect rect, const ThemeMetrics& metrics,
                      std::vector<ThemeLayoutSlot>& slots) {
  if (node.children.empty()) {
    if (!node.id.empty()) slots.push_back(ThemeLayoutSlot{node.id, rect});
    return;
  }

  const auto childCount = static_cast<uint8_t>(std::min<size_t>(node.children.size(), UINT8_MAX));
  freeink::ui::layoutLinear(
      toUiRect(rect), toUiAxis(node.axis), static_cast<int16_t>(node.gap), childCount,
      [&](uint8_t i) { return toUiLength(node.children[i], metrics); },
      [&](uint8_t i, freeink::ui::Rect childRect) { layoutThemeSlots(node.children[i], fromUiRect(childRect), metrics, slots); });
}

Rect findThemeSlot(const std::vector<ThemeLayoutSlot>& slots, const std::string& id) {
  for (const auto& slot : slots) {
    if (slot.id == id) return slot.rect;
  }
  return Rect{};
}
