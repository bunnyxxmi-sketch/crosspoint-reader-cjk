#include "EpubReaderMenuActivity.h"

#include <cstdio>
#include <FontManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kBuiltinReaderFontCount = 3;
constexpr CrossPointSettings::FONT_FAMILY kBuiltinReaderFonts[kBuiltinReaderFontCount] = {
    CrossPointSettings::BOOKERLY, CrossPointSettings::NOTOSANS, CrossPointSettings::OPENDYSLEXIC};
constexpr StrId kBuiltinReaderFontLabels[kBuiltinReaderFontCount] = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS,
                                                                     StrId::STR_OPEN_DYSLEXIC};
}  // namespace

void EpubReaderMenuActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  skipNextButtonCheck = true;
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { ActivityWithSubactivity::onExit(); }

void EpubReaderMenuActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (skipNextButtonCheck) {
    const bool confirmCleared = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backCleared = !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                             !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmCleared && backCleared) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  // Use local variables for items we need to check after potential deletion
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      // Cycle orientation preview locally; actual rotation happens on menu exit.
      pendingOrientation = (pendingOrientation + 1) % orientationLabels.size();
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::STYLE_FIRST_LINE_INDENT || selectedAction == MenuAction::STYLE_INVERT_IMAGES) {
      // Toggle-style actions keep the reader menu open; refresh the row value in-place.
      onAction(selectedAction);
      requestUpdate();
      return;
    }

    // 1. Capture the callback and action locally
    auto actionCallback = onAction;

    // 2. Execute the callback
    actionCallback(selectedAction);

    // 3. CRITICAL: Return immediately. 'this' is likely deleted now.
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Return the pending orientation to the parent so it can apply on exit.
    onBack(pendingOrientation);
    return;  // Also return here just in case
  }
}

void EpubReaderMenuActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: button hints are drawn along a vertical edge, so we
  // reserve a horizontal gutter to prevent overlap with menu content.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: button hints appear near the logical top, so we reserve
  // vertical space to keep the header and list clear.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  // Title
  const std::string truncTitle =
      renderer.truncatedText(UI_12_FONT_ID, title.c_str(), contentWidth - 40, EpdFontFamily::BOLD);
  // Manual centering so we can respect the content gutter.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Progress summary
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, 45 + contentY, progressLine.c_str());

  // Menu Items
  const int startY = 75 + contentY;
  constexpr int lineHeight = 30;

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int displayY = startY + (i * lineHeight);
    const bool isSelected = (static_cast<int>(i) == selectedIndex);

    if (isSelected) {
      // Highlight only the content area so we don't paint over hint gutters.
      renderer.fillRect(contentX, displayY, contentWidth - 1, lineHeight, true);
    }

    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, I18N.get(menuItems[i].labelId), !isSelected);

    const std::string value = getMenuItemValue(menuItems[i].action);
    if (!value.empty()) {
      const auto width = renderer.getTextWidth(UI_10_FONT_ID, value.c_str());
      renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - 20 - width, displayY, value.c_str(), !isSelected);
    }
  }

  // Footer / Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

std::string EpubReaderMenuActivity::getCurrentFontLabel() const {
  const int currentExternal = FontMgr.getSelectedIndex();
  if (currentExternal >= 0) {
    const FontInfo* info = FontMgr.getFontInfo(currentExternal);
    return info ? std::string(info->name) : std::string(tr(STR_EXTERNAL_FONT));
  }

  for (int i = 0; i < kBuiltinReaderFontCount; ++i) {
    if (SETTINGS.fontFamily == static_cast<uint8_t>(kBuiltinReaderFonts[i])) {
      return std::string(I18N.get(kBuiltinReaderFontLabels[i]));
    }
  }
  return std::string(I18N.get(kBuiltinReaderFontLabels[0]));
}

std::string EpubReaderMenuActivity::getMenuItemValue(const MenuAction action) const {
  switch (action) {
    case MenuAction::ROTATE_SCREEN:
      return std::string(I18N.get(orientationLabels[pendingOrientation]));
    case MenuAction::STYLE_FIRST_LINE_INDENT:
      return SETTINGS.firstLineIndent ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
    case MenuAction::STYLE_INVERT_IMAGES:
      return SETTINGS.invertImages ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
    case MenuAction::STYLE_FONT_FAMILY:
      return getCurrentFontLabel();
    case MenuAction::STYLE_LINE_SPACING: {
      char spacingBuf[16];
      snprintf(spacingBuf, sizeof(spacingBuf), "%.2fx", static_cast<float>(SETTINGS.lineSpacing) / 100.0f);
      return spacingBuf;
    }
    case MenuAction::STYLE_STATUS_BAR:
      return "";
    default:
      return "";
  }
}
