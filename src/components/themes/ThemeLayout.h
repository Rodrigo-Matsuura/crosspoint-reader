#pragma once

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"

enum class ThemeLayoutAxis { Column, Row };
enum class ThemeLayoutSizeType { Flex, Fixed, Token };
enum class ThemeScreenKind { Home, FileBrowser, RecentBooks, Settings, Reader };
enum class ThemeHomeWidgetType { Header, HeaderTitle, Battery, Clock, Recents, LauncherList, LauncherGrid, ButtonHints };
enum class ThemeHomeAction { FileBrowser, RecentBooks, OpdsBrowser, FileTransfer, Settings, RecentBook };

struct ThemeLayoutNode {
  std::string id;
  ThemeLayoutAxis axis = ThemeLayoutAxis::Column;
  int gap = 0;
  ThemeLayoutSizeType sizeType = ThemeLayoutSizeType::Flex;
  int size = 0;
  int flex = 1;
  std::string sizeToken;
  std::vector<ThemeLayoutNode> children;
};

struct ThemeHomeLauncherSpec {
  std::string text;
  UIIcon icon = UIIcon::None;
  ThemeHomeAction action = ThemeHomeAction::FileBrowser;
};

struct ThemeHomeWidgetSpec {
  std::string slot;
  ThemeHomeWidgetType type = ThemeHomeWidgetType::LauncherList;
  int columns = 1;
  int rows = 0;
  int gap = 0;
  std::vector<ThemeHomeLauncherSpec> launchers;
};

struct ThemeHomeScreenSpec {
  bool enabled = false;
  ThemeLayoutNode layout;
  std::vector<ThemeHomeWidgetSpec> widgets;
};

struct ThemeScreenSpec {
  bool enabled = false;
  ThemeLayoutNode layout;
};

struct ThemeLayoutSlot {
  std::string id;
  Rect rect;
};

int themeLayoutTokenSize(const ThemeMetrics& metrics, const std::string& token);
void layoutThemeSlots(const ThemeLayoutNode& node, Rect rect, const ThemeMetrics& metrics,
                      std::vector<ThemeLayoutSlot>& slots);
Rect findThemeSlot(const std::vector<ThemeLayoutSlot>& slots, const std::string& id);
