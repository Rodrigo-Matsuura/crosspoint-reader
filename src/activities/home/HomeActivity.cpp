#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

const char* defaultLauncherLabel(ThemeHomeAction action) {
  switch (action) {
    case ThemeHomeAction::RecentBooks:
      return tr(STR_MENU_RECENT_BOOKS);
    case ThemeHomeAction::OpdsBrowser:
      return tr(STR_OPDS_BROWSER);
    case ThemeHomeAction::FileTransfer:
      return tr(STR_FILE_TRANSFER);
    case ThemeHomeAction::Settings:
      return tr(STR_SETTINGS_TITLE);
    case ThemeHomeAction::RecentBook:
      return tr(STR_CONTINUE_READING);
    case ThemeHomeAction::FileBrowser:
    default:
      return tr(STR_BROWSE_FILES);
  }
}

UIIcon defaultLauncherIcon(ThemeHomeAction action) {
  switch (action) {
    case ThemeHomeAction::RecentBooks:
      return UIIcon::Recent;
    case ThemeHomeAction::OpdsBrowser:
      return UIIcon::Library;
    case ThemeHomeAction::FileTransfer:
      return UIIcon::Transfer;
    case ThemeHomeAction::Settings:
      return UIIcon::Settings;
    case ThemeHomeAction::RecentBook:
      return UIIcon::Book;
    case ThemeHomeAction::FileBrowser:
    default:
      return UIIcon::Folder;
  }
}

bool actionVisible(ThemeHomeAction action, bool hasOpdsServers, bool hasRecentBooks) {
  if (action == ThemeHomeAction::OpdsBrowser) return hasOpdsServers;
  if (action == ThemeHomeAction::RecentBook) return hasRecentBooks;
  return true;
}

std::string homeHeaderTitle(const ThemeMetrics& metrics, const std::vector<RecentBook>& recentBooks,
                            const int coverSelectorIndex) {
  if (metrics.homeContinueReadingInMenu && metrics.homeShowContinueReadingHeader && !recentBooks.empty()) {
    return recentBooks[std::min(coverSelectorIndex, static_cast<int>(recentBooks.size()) - 1)].title;
  }
  return "";
}

}  // namespace

void HomeActivity::buildHomeActions(std::vector<HomeActionEntry>& actions) const {
  actions.clear();
  const ThemeHomeScreenSpec* spec = UITheme::getInstance().getHomeScreenSpec();
  if (spec != nullptr) {
    for (const auto& widget : spec->widgets) {
      if (widget.type == ThemeHomeWidgetType::Recents) {
        for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
          actions.push_back(HomeActionEntry{ThemeHomeAction::RecentBook, i});
        }
      } else if (widget.type == ThemeHomeWidgetType::LauncherList || widget.type == ThemeHomeWidgetType::LauncherGrid) {
        for (const auto& launcher : widget.launchers) {
          if (actionVisible(launcher.action, hasOpdsServers, !recentBooks.empty())) {
            actions.push_back(HomeActionEntry{launcher.action, 0});
          }
        }
      }
    }
    if (!actions.empty()) return;
  }

  for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
    actions.push_back(HomeActionEntry{ThemeHomeAction::RecentBook, i});
  }
  actions.push_back(HomeActionEntry{ThemeHomeAction::FileBrowser, 0});
  actions.push_back(HomeActionEntry{ThemeHomeAction::RecentBooks, 0});
  if (hasOpdsServers) actions.push_back(HomeActionEntry{ThemeHomeAction::OpdsBrowser, 0});
  actions.push_back(HomeActionEntry{ThemeHomeAction::FileTransfer, 0});
  actions.push_back(HomeActionEntry{ThemeHomeAction::Settings, 0});
}

int HomeActivity::getMenuItemCount() const {
  std::vector<HomeActionEntry> actions;
  buildHomeActions(actions);
  if (!actions.empty()) return static_cast<int>(actions.size());

  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(const std::vector<int>& coverHeights) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      bool hasMissingThumb = false;
      for (const int coverHeight : coverHeights) {
        std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
        if (!Storage.exists(coverPath.c_str())) {
          hasMissingThumb = true;
          break;
        }
      }

      if (hasMissingThumb) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = true;
          for (const int coverHeight : coverHeights) {
            std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
            if (!Storage.exists(coverPath.c_str())) {
              success = epub.generateThumbBmp(coverHeight) && success;
            }
          }
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = true;
            for (const int coverHeight : coverHeights) {
              std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
              if (!Storage.exists(coverPath.c_str())) {
                success = xtc.generateThumbBmp(coverHeight) && success;
              }
            }
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  LOG_DBG("HOME", "Loaded %d/%d recent book(s) for home theme", static_cast<int>(recentBooks.size()),
          metrics.homeRecentBooksCount);

  const ThemeHomeScreenSpec* homeSpec = UITheme::getInstance().getHomeScreenSpec();
  if (homeSpec != nullptr) {
    std::vector<HomeActionEntry> actions;
    buildHomeActions(actions);
    selectorIndex = 0;
    const auto wantedAction = [this]() {
      switch (initialMenuItem) {
        case HomeMenuItem::RECENTS:
          return ThemeHomeAction::RecentBooks;
        case HomeMenuItem::OPDS_BROWSER:
          return ThemeHomeAction::OpdsBrowser;
        case HomeMenuItem::FILE_TRANSFER:
          return ThemeHomeAction::FileTransfer;
        case HomeMenuItem::SETTINGS_MENU:
          return ThemeHomeAction::Settings;
        case HomeMenuItem::FILE_BROWSER:
        case HomeMenuItem::NONE:
        default:
          return ThemeHomeAction::FileBrowser;
      }
    }();
    if (initialMenuItem != HomeMenuItem::NONE) {
      for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        if (actions[i].action == wantedAction) {
          selectorIndex = i;
          break;
        }
      }
    }
  } else {
    const auto base = static_cast<int>(recentBooks.size());
    selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);
  }
  coverSelectorIndex = recentBooks.empty() ? 0 : std::min(selectorIndex, static_cast<int>(recentBooks.size()) - 1);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = makeUniqueNoThrow<uint8_t[]>(needed);
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                   coverBufferSize)) {
    coverBuffer.reset();
    coverBufferSize = 0;
    return false;
  }
  coverBufferSelectorIndex = coverSelectorIndex;
  std::vector<HomeActionEntry> actions;
  buildHomeActions(actions);
  coverBufferStripSelected = selectorIndex >= 0 && selectorIndex < static_cast<int>(actions.size()) &&
                             actions[selectorIndex].action == ThemeHomeAction::RecentBook;
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                     coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  coverBuffer.reset();
  coverBufferSize = 0;
  coverBufferStored = false;
  coverBufferSelectorIndex = -1;
  coverBufferStripSelected = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    std::vector<HomeActionEntry> actions;
    buildHomeActions(actions);
    if (selectorIndex < static_cast<int>(actions.size()) &&
        actions[selectorIndex].action == ThemeHomeAction::RecentBook) {
      coverSelectorIndex = actions[selectorIndex].value;
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    std::vector<HomeActionEntry> actions;
    buildHomeActions(actions);
    if (selectorIndex < static_cast<int>(actions.size()) &&
        actions[selectorIndex].action == ThemeHomeAction::RecentBook) {
      coverSelectorIndex = actions[selectorIndex].value;
    }
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    std::vector<HomeActionEntry> actions;
    buildHomeActions(actions);
    if (selectorIndex < 0 || selectorIndex >= static_cast<int>(actions.size())) return;
    const auto& entry = actions[selectorIndex];
    switch (entry.action) {
      case ThemeHomeAction::RecentBook:
        if (entry.value >= 0 && entry.value < static_cast<int>(recentBooks.size()))
          onSelectBook(recentBooks[entry.value].path);
        break;
      case ThemeHomeAction::RecentBooks:
        onRecentsOpen();
        break;
      case ThemeHomeAction::OpdsBrowser:
        onOpdsBrowserOpen();
        break;
      case ThemeHomeAction::FileTransfer:
        onFileTransferOpen();
        break;
      case ThemeHomeAction::Settings:
        onSettingsOpen();
        break;
      case ThemeHomeAction::FileBrowser:
      default:
        onFileBrowserOpen();
        break;
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  constexpr int coverCacheBleed = 12;
  const ThemeHomeScreenSpec* homeSpec = UITheme::getInstance().getHomeScreenSpec();

  if (homeSpec != nullptr) {
    std::vector<ThemeLayoutSlot> slots;
    layoutThemeSlots(homeSpec->layout, Rect{0, 0, pageWidth, pageHeight}, metrics, slots);
    std::vector<HomeActionEntry> actions;
    buildHomeActions(actions);

    renderer.clearScreen();

    coverRectX = 0;
    coverRectY = 0;
    coverRectW = 0;
    coverRectH = 0;

    int actionOffset = 0;
    for (const auto& widget : homeSpec->widgets) {
      Rect slot = findThemeSlot(slots, widget.slot);
      if (slot.width <= 0 || slot.height <= 0) continue;

      if (widget.type == ThemeHomeWidgetType::Header) {
        const auto title = homeHeaderTitle(metrics, recentBooks, coverSelectorIndex);
        GUI.drawHeader(renderer, slot, title.empty() ? nullptr : title.c_str());
      } else if (widget.type == ThemeHomeWidgetType::HeaderTitle) {
        const auto title = homeHeaderTitle(metrics, recentBooks, coverSelectorIndex);
        if (!title.empty()) {
          const auto truncated = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), slot.width);
          const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, truncated.c_str());
          renderer.drawText(UI_10_FONT_ID, slot.x + std::max(0, (slot.width - textWidth) / 2),
                            slot.y + std::max(0, (slot.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2),
                            truncated.c_str());
        }
      } else if (widget.type == ThemeHomeWidgetType::Battery) {
        const bool showBatteryPercentage =
            SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
        const int batteryX = slot.x + std::max(0, slot.width - metrics.batteryWidth);
        GUI.drawBatteryRight(renderer, Rect{batteryX, slot.y, metrics.batteryWidth, metrics.batteryHeight},
                             showBatteryPercentage);
      } else if (widget.type == ThemeHomeWidgetType::Clock) {
        if (halClock.isAvailable()) {
          char timeBuf[9];
          if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
            auto clockText = renderer.truncatedText(UI_10_FONT_ID, timeBuf, slot.width);
            const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, clockText.c_str());
            renderer.drawText(UI_10_FONT_ID, slot.x + std::max(0, (slot.width - textWidth) / 2),
                              slot.y + std::max(0, (slot.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2),
                              clockText.c_str());
          }
        }
      } else if (widget.type == ThemeHomeWidgetType::Recents) {
        const bool hasCoverArea = slot.height > 0 && metrics.homeCoverHeight > 0;
        coverRectX = 0;
        coverRectY = std::max(0, slot.y - coverCacheBleed);
        coverRectW = pageWidth;
        coverRectH = std::min(pageHeight - coverRectY, slot.height + (slot.y - coverRectY) + coverCacheBleed);

        const bool selectorSensitiveCoverCache = GUI.homeCoverCacheDependsOnSelector();
        const bool coverStripSelected =
            selectorIndex >= actionOffset && selectorIndex < actionOffset + static_cast<int>(recentBooks.size());
        if (coverStripSelected) {
          coverSelectorIndex = actions[selectorIndex].value;
        }
        bool bufferRestored = hasCoverArea && coverBufferStored &&
                              (!selectorSensitiveCoverCache || (coverBufferSelectorIndex == coverSelectorIndex &&
                                                                coverBufferStripSelected == coverStripSelected)) &&
                              restoreCoverBuffer();

        if (hasCoverArea) {
          GUI.drawRecentBookCover(renderer, slot, recentBooks, coverSelectorIndex, coverRendered, coverBufferStored,
                                  bufferRestored, std::bind(&HomeActivity::storeCoverBuffer, this), coverStripSelected);
        }
        actionOffset += static_cast<int>(recentBooks.size());
      } else if (widget.type == ThemeHomeWidgetType::LauncherList || widget.type == ThemeHomeWidgetType::LauncherGrid) {
        std::vector<ThemeHomeLauncherSpec> launchers;
        for (const auto& launcher : widget.launchers) {
          if (actionVisible(launcher.action, hasOpdsServers, !recentBooks.empty())) {
            launchers.push_back(launcher);
          }
        }
        const int selectedLocal =
            selectorIndex >= actionOffset && selectorIndex < actionOffset + static_cast<int>(launchers.size())
                ? selectorIndex - actionOffset
                : -1;

        if (widget.type == ThemeHomeWidgetType::LauncherGrid) {
          const int columns = std::max(1, widget.columns);
          const int rows =
              widget.rows > 0 ? widget.rows : std::max(1, (static_cast<int>(launchers.size()) + columns - 1) / columns);
          const int gap = std::max(0, widget.gap);
          const int cellW = std::max(1, (slot.width - gap * (columns - 1)) / columns);
          const int cellH = std::max(1, (slot.height - gap * (rows - 1)) / rows);
          for (int i = 0; i < static_cast<int>(launchers.size()); ++i) {
            const int col = i % columns;
            const int row = i / columns;
            if (row >= rows) break;
            Rect cell{slot.x + col * (cellW + gap), slot.y + row * (cellH + gap),
                      col == columns - 1 ? slot.x + slot.width - (slot.x + col * (cellW + gap)) : cellW, cellH};
            GUI.drawButtonMenu(
                renderer, cell, 1, selectedLocal == i ? 0 : -1,
                [&launchers, i](int) {
                  return launchers[i].text.empty() ? std::string(defaultLauncherLabel(launchers[i].action))
                                                   : launchers[i].text;
                },
                [&launchers, i](int) {
                  return launchers[i].icon == UIIcon::None ? defaultLauncherIcon(launchers[i].action)
                                                           : launchers[i].icon;
                });
          }
        } else {
          GUI.drawButtonMenu(
              renderer, slot, static_cast<int>(launchers.size()), selectedLocal,
              [&launchers](int index) {
                return launchers[index].text.empty() ? std::string(defaultLauncherLabel(launchers[index].action))
                                                     : launchers[index].text;
              },
              [&launchers](int index) {
                return launchers[index].icon == UIIcon::None ? defaultLauncherIcon(launchers[index].action)
                                                             : launchers[index].icon;
              });
        }
        actionOffset += static_cast<int>(launchers.size());
      } else if (widget.type == ThemeHomeWidgetType::ButtonHints) {
        const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
    }

    renderer.displayBuffer();

    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (metrics.homeCoverHeight > 0 && !recentsLoaded && !recentsLoading) {
      recentsLoading = true;
      loadRecentCovers(UITheme::getInstance().getHomeCoverThumbHeights());
    }
    return;
  }

  const bool hasCoverArea = metrics.homeCoverTileHeight > 0 && metrics.homeCoverHeight > 0;

  renderer.clearScreen();

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. Include a small bleed
  // because cover-strip themes can draw selection outlines just outside the
  // nominal cover tile.
  coverRectX = 0;
  coverRectY = hasCoverArea ? std::max(0, metrics.homeTopPadding - coverCacheBleed) : 0;
  coverRectW = pageWidth;
  coverRectH = hasCoverArea
                   ? std::min(pageHeight - coverRectY,
                              metrics.homeCoverTileHeight + (metrics.homeTopPadding - coverRectY) + coverCacheBleed)
                   : 0;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && metrics.homeShowContinueReadingHeader && !recentBooks.empty()
                     ? recentBooks[std::min(coverSelectorIndex, static_cast<int>(recentBooks.size()) - 1)].title.c_str()
                     : nullptr);

  const bool selectorSensitiveCoverCache = GUI.homeCoverCacheDependsOnSelector();
  const bool coverStripSelected = selectorIndex < static_cast<int>(recentBooks.size());
  bool bufferRestored = hasCoverArea && coverBufferStored &&
                        (!selectorSensitiveCoverCache || (coverBufferSelectorIndex == coverSelectorIndex &&
                                                          coverBufferStripSelected == coverStripSelected)) &&
                        restoreCoverBuffer();

  if (hasCoverArea) {
    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, coverSelectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this), coverStripSelected);
  } else {
    coverRendered = false;
    coverBufferStored = false;
    bufferRestored = false;
  }

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (metrics.homeCoverHeight > 0 && !recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(UITheme::getInstance().getHomeCoverThumbHeights());
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
