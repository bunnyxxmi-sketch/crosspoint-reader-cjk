#pragma once
#include <Epub.h>

#include <functional>
#include <memory>

#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "activities/ActivityWithSubactivity.h"

class KOReaderSyncActivity final : public ActivityWithSubactivity {
 public:
  using OnCancelCallback = std::function<void()>;
  using OnSyncCompleteCallback = std::function<void(int newSpineIndex, int newPageNumber)>;

  explicit KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::shared_ptr<Epub>& epub, const std::string& epubPath, int currentSpineIndex,
                                int currentPage, int totalPagesInSpine, OnCancelCallback onCancel,
                                OnSyncCompleteCallback onSyncComplete)
      : ActivityWithSubactivity("KOReaderSync", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine),
        remoteProgress{},
        remotePosition{},
        localProgress{},
        onCancel(std::move(onCancel)),
        onSyncComplete(std::move(onSyncComplete)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }
  bool supportsLandscape() const override { return true; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;

  bool hasRemoteProgress = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;
  KOReaderPosition localProgress;

  int selectedOption = 0;
  bool freedFontCache = false;

  OnCancelCallback onCancel;
  OnSyncCompleteCallback onSyncComplete;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
};
