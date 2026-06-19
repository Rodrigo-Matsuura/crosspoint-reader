#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub ? epub->getTocItemsCount() : 0; }

void EpubReaderChapterSelectionActivity::cancelAndFinish() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderChapterSelectionActivity::activateSelection(int index) {
  const int totalItems = getTotalItems();
  if (!epub || totalItems <= 0) {
    cancelAndFinish();
    return;
  }

  index = std::clamp(index, 0, totalItems - 1);
  const auto tocItem = epub->getTocItem(index);
  if (tocItem.spineIndex < 0 || tocItem.spineIndex >= epub->getSpineItemsCount()) {
    cancelAndFinish();
    return;
  }

  selectorIndex = index;
  setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
  finish();
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int totalItems = getTotalItems();
  if (!epub || totalItems <= 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelAndFinish();
    }
    return;
  }

  // Vertical swipe page-scrolls the list (touch nav without the side buttons).
  if (mappedInput.wasListScroll(selectorIndex, totalItems, pageItems)) {
    requestUpdate();
    return;
  }

  int downId = -1;
  if (mappedInput.wasItemTouchedDown(downId) && downId >= 0 && downId < totalItems) {
    selectorIndex = downId;
    requestUpdate();
  }

  int tappedId = -1;
  const bool tapped = mappedInput.wasItemTapped(tappedId);
  const bool validTap = tapped && tappedId >= 0 && tappedId < totalItems;
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (validTap) {
    selectorIndex = tappedId;
  }
  if (validTap || confirm) {
    activateSelection(selectorIndex);
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelAndFinish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const int totalItems = getTotalItems();
  const int displayIndex = totalItems > 0 ? std::clamp(selectorIndex, 0, totalItems - 1) : 0;
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, displayIndex,
               [this](int index) {
                 auto item = epub->getTocItem(index);
                 const int level = item.level > 0 ? item.level - 1 : 0;
                 std::string indent(level * 2, ' ');
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
