// Copyright 2026 The Xplorer Authors.
// Tab screenshot: native view snapshot with compositor fallback.

#include "chrome/browser/agent_gateway/tab_screenshot.h"

#include <memory>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_capture_info.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/snapshot/snapshot.h"

namespace agent_gateway {

namespace {

// Whether |wc| is the active tab of a visible window. Agent captures must NOT
// foreground a tab (that would steal focus from the user — the last focus-steal
// hole), so instead of activating/raising the tab we only probe its current
// state: a visible/active tab can be grabbed straight off its native view; a
// hidden/background tab goes through the compositor path, which keeps its
// renderer producing frames via IncrementCapturerCount without raising the
// window. This is read-only and changes nothing about tab/window state.
bool IsTabCurrentlyVisible(content::WebContents* wc) {
  if (!wc)
    return false;
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    TabStripModel* model = browser->GetTabStripModel();
    if (model->GetActiveWebContents() != wc)
      continue;
    ui::BaseWindow* window = browser->GetWindow();
    return window && window->IsVisible();
  }
  return false;
}

void ReplyPng(base::OnceCallback<void(base::DictValue)> cb,
              const std::vector<uint8_t>& png) {
  base::DictValue out;
  out.Set("data", base::Base64Encode(png));
  std::move(cb).Run(std::move(out));
}

void ReplyError(base::OnceCallback<void(base::DictValue)> cb,
                const std::string& error) {
  base::DictValue err;
  err.Set("error", error);
  std::move(cb).Run(std::move(err));
}

void CaptureViaCompositor(
    content::WebContents* web_contents,
    base::OnceCallback<void(base::DictValue)> callback) {
  auto hold = web_contents->IncrementCapturerCount(
      gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/true,
      /*is_activity=*/true);

  content::RenderWidgetHostView* view =
      web_contents->GetRenderWidgetHostView();
  if (!view || !view->IsSurfaceAvailableForCopy()) {
    ReplyError(std::move(callback), "surface not available");
    return;
  }

  gfx::Size source_size = view->GetViewBounds().size();
  if (source_size.IsEmpty()) {
    ReplyError(std::move(callback), "empty view");
    return;
  }

  ThumbnailCaptureInfo info;
  info.source_size = source_size;
  info.copy_rect = gfx::Rect(source_size);
  info.target_size = source_size;

  // EnsureSurfaceSynchronizedForWebTest() was removed from the public
  // RenderWidgetHostView API upstream; CopyFromSurface captures the current
  // surface directly.
  view->CopyFromSurface(
      info.copy_rect, info.target_size, base::Seconds(8),
      base::BindOnce(
          [](base::ScopedClosureRunner hold,
             base::OnceCallback<void(base::DictValue)> cb,
             const content::CopyFromSurfaceResult& result) {
            hold.RunAndReset();
            if (!result.has_value() || result->bitmap.drawsNothing()) {
              ReplyError(std::move(cb),
                          "capture failed — grant Screen Recording to Xplorer "
                          "in System Settings › Privacy & Security");
              return;
            }
            std::optional<std::vector<uint8_t>> png =
                gfx::PNGCodec::EncodeBGRASkBitmap(result->bitmap,
                                                  /*discard_transparency=*/false);
            if (!png || png->empty()) {
              ReplyError(std::move(cb), "png encode failed");
              return;
            }
            ReplyPng(std::move(cb), *png);
          },
          std::move(hold), std::move(callback)));
}

}  // namespace

void CaptureTabScreenshot(
    content::WebContents* web_contents,
    base::OnceCallback<void(base::DictValue)> callback) {
  if (!web_contents) {
    ReplyError(std::move(callback), "no web contents");
    return;
  }

  // Never activate the tab or raise/Show the window: that would steal focus
  // from whatever the user is doing. Only a tab that is ALREADY the active tab
  // of a visible window can be grabbed off its native view (an occluded view
  // has no up-to-date snapshot). Any hidden/background tab — the common case
  // for agent-opened background tabs — goes straight to the compositor path,
  // which holds a capturer count so the renderer produces frames while hidden.
  if (!IsTabCurrentlyVisible(web_contents)) {
    CaptureViaCompositor(web_contents, std::move(callback));
    return;
  }

  gfx::Rect region(web_contents->GetSize());
  if (region.IsEmpty()) {
    CaptureViaCompositor(web_contents, std::move(callback));
    return;
  }

  ui::GrabViewSnapshot(
      web_contents->GetNativeView(), region,
      base::BindOnce(
          [](content::WebContents* wc,
             base::OnceCallback<void(base::DictValue)> cb, gfx::Image image) {
            if (!image.IsEmpty()) {
              scoped_refptr<base::RefCountedMemory> png = image.As1xPNGBytes();
              if (png && png->size() > 0) {
                ReplyPng(std::move(cb),
                         std::vector<uint8_t>(png->begin(), png->end()));
                return;
              }
            }
            CaptureViaCompositor(wc, std::move(cb));
          },
          web_contents, std::move(callback)));
}

}  // namespace agent_gateway