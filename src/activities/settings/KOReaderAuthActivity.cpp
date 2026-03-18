#include "KOReaderAuthActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "FontManager.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KOReaderAuthActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = tr(STR_AUTHENTICATING);
  }
  requestUpdate();

  performAuthentication();
}

void KOReaderAuthActivity::performAuthentication() {
  const auto result = KOReaderSyncClient::authenticate();

  {
    RenderLock lock(*this);
    if (result == KOReaderSyncClient::OK) {
      state = SUCCESS;
      statusMessage = tr(STR_AUTH_SUCCESS);
    } else {
      state = FAILED;
      errorMessage = KOReaderSyncClient::errorString(result);
    }
  }
  requestUpdate();
}

void KOReaderAuthActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  // Free ALL external font caches to reclaim ~70KB heap for WiFi + TLS
  renderer.clearFontCache();
  auto& fm = FontManager::getInstance();
  ExternalFont* rf = fm.getActiveFont();
  ExternalFont* uf = fm.getActiveUiFont();
  if (rf) {
    rf->unload();
    freedReaderFontCache = true;
  }
  if (uf && uf != rf) {
    uf->unload();
  }

  // Turn on WiFi
  WiFi.mode(WIFI_STA);

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    state = AUTHENTICATING;
    statusMessage = tr(STR_AUTHENTICATING);
    requestUpdate();

    // Perform authentication in a separate task
    xTaskCreate(
        [](void* param) {
          auto* self = static_cast<KOReaderAuthActivity*>(param);
          self->performAuthentication();
          vTaskDelete(nullptr);
        },
        "AuthTask", 8192, this, 1, nullptr);
    return;
  }

  // Launch WiFi selection
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void KOReaderAuthActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Turn off wifi
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Reload only reader font (skip UI font to preserve heap for chapter loading)
  if (freedReaderFontCache) {
    auto& fm = FontManager::getInstance();
    int rIdx = fm.getSelectedIndex();
    if (rIdx >= 0 && rIdx < fm.getFontCount()) {
      const FontInfo* info = fm.getFontInfo(rIdx);
      if (info) {
        char path[96];
        snprintf(path, sizeof(path), "/fonts/%s", info->filename);
        // getActiveFont() returns nullptr after unload since isLoaded()=false
        // We need to access _activeFont directly - use selectFont with a trick
        fm.selectFont(-1);   // clear index
        fm.selectFont(rIdx); // triggers loadSelectedFont()
      }
    }
    freedReaderFontCache = false;
  }
}

void KOReaderAuthActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_KOREADER_AUTH));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  if (state == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str());
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_SUCCESS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, tr(STR_SYNC_READY));
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_FAILED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, errorMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KOReaderAuthActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      onComplete();
    }
  }
}
