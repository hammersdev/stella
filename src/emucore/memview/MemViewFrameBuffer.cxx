//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2026 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include "Logger.hxx"
#include "OSystem.hxx"
#include "Settings.hxx"
#include "MediaFactory.hxx"
#include "ToolTip.hxx"
#include "MemView.hxx"
#include "MemViewFrameBuffer.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewFrameBuffer::MemViewFrameBuffer(OSystem& osystem)
  : FrameBuffer(osystem)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewFrameBuffer::~MemViewFrameBuffer() = default;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus MemViewFrameBuffer::createDisplay(string_view title, BufferType type,
                                        Common::Size size, bool honourHiDPI)
{
  ++myInitializedCount;
  myBackend->setTitle(title);

  // Always save, maybe only the mode of the window has changed
  saveCurrentWindowPosition();
  myBufferType = type;

  // In HiDPI mode, all created displays must be scaled appropriately
  if(honourHiDPI && hidpiEnabled())
  {
    size.w *= hidpiScaleFactor();
    size.h *= hidpiScaleFactor();
  }

  // A 'windowed' system is defined as one where the window size can be
  // larger than the screen size, as there's some sort of window manager
  // that takes care of it (all current desktop systems fall in this category)
  // However, some systems have no concept of windowing, and have hard limits
  // on how large a window can be (ie, the size of the 'desktop' is the
  // absolute upper limit on window size)
  //
  // If the WINDOWED_SUPPORT macro is defined, we treat the system as the
  // former type; if not, as the latter type

  const int display = displayId();
#ifdef WINDOWED_SUPPORT
  // We assume that a desktop of at least minimum acceptable size means that
  // we're running on a 'large' system, and the window size requirements
  // can be relaxed
  // Otherwise, we treat the system as if WINDOWED_SUPPORT is not defined
  if(myDesktopSize[display].w < FBMinimum::Width &&
     myDesktopSize[display].h < FBMinimum::Height &&
     size > myDesktopSize[display])
  {
    return FBInitStatus::FailTooLarge;
  }
#else
  // Make sure this mode is even possible
  // We only really need to worry about it in non-windowed environments,
  // where requesting a window that's too large will probably cause a crash
  if(size > myDesktopSize[display])
  {
    return FBInitStatus::FailTooLarge;
  }
#endif

  myMsgHandler.init();

  // Initialize video mode handler, so it can know what video modes are
  // appropriate for the requested image size
  myVidModeHandler.setImageSize(size);

  // Initialize video subsystem
  const string pre_about = myBackend->about();
  const FBInitStatus status = applyVideoMode();

  if(status != FBInitStatus::Success)
    return status;

  // Print initial usage message, but only print it later if the status has changed
  if(myInitializedCount == 1)
  {
    Logger::info(myBackend->about());
  }
  else
  {
    const string post_about = myBackend->about();
    if(post_about != pre_about)
      Logger::info(post_about);
  }

  return status;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewFrameBuffer::update(UpdateMode mode)
{
  // Onscreen messages are a special case and require different handling than
  // other objects; they aren't UI dialogs in the normal sense nor are they
  // TIA images, and they need to be rendered on top of everything
  // The logic is split in two pieces:
  //  - at the top of ::update(), to determine whether underlying dialogs
  //    need to be force-redrawn
  //  - at the bottom of ::update(), to actually draw them (this must come
  //    last, since they are always drawn on top of everything else).

  const bool forceRedraw = (mode == UpdateMode::REDRAW);
  bool redraw = forceRedraw;

  // Forced render without draw required if messages or dialogs were closed
  // Note: For dialogs only relevant when two or more dialogs were stacked
  const bool rerender = (mode == UpdateMode::REDRAW || mode == UpdateMode::RERENDER)
    || myPendingRender;

  myOSystem.memView().setForcedUpdate(rerender);

  myPendingRender = false;

  // Show any messages enqueued from other threads (e.g. PlusROM/cart callbacks)
//  myMsgHandler.drainPending();

  // Tick the MemView (update data and access counters)
  myOSystem.memView().tick();

  // Ask the GUI if it needs to be redrawn
  redraw |= myOSystem.memView().needsRedraw();

  if(redraw)
    // Redraw, update and render everything dirty
    myOSystem.memView().draw(forceRedraw);
  else if(rerender)
    // Only render
    myOSystem.memView().render();

  // Draw any pending messages
  // The logic here determines whether to draw the message
  // If the message is to be disabled, logic inside the draw method
  // indicates that, and then the code at the top of this method sees
  // the change and redraws everything

  if(myMsgHandler.isShown())
    redraw |= myMsgHandler.draw();

  // Push buffers to screen only when necessary
  if(redraw || rerender)
    myBackend->renderToScreen();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus MemViewFrameBuffer::applyVideoMode()
{
  // Update display size, in case windowed/fullscreen mode has changed
  const Settings& s = myOSystem.settings();
  const int ID = displayId(); // TODO SDL 3:

  if(s.getBool("fullscreen"))
    myVidModeHandler.setDisplaySize(myFullscreenDisplays[ID], true);
  else
    myVidModeHandler.setDisplaySize(myAbsDesktopSize[ID], false);

  // Build the new mode based on current settings
  const VideoModeHandler::Mode& mode
    = myVidModeHandler.buildMode(s, false, Bezel::Info());
  if(mode.imageR.size() > mode.screenS)
    return FBInitStatus::FailTooLarge;

  // Changing the video mode can take some time, during which the last
  // sound played may get 'stuck'
  // So we pause the sound until the operation completes
  const bool oldPauseState = myOSystem.sound().pause(true);
  FBInitStatus status = FBInitStatus::FailNotSupported;

  if(myBackend->setVideoMode(mode,
      myOSystem.settings().getInt(getDisplayKey()),
      myOSystem.settings().getPoint(getPositionKey()))
    )
  {
    myActiveVidMode = mode;
    status = FBInitStatus::Success;

    // Did we get the requested fullscreen state?
    myOSystem.settings().setValue("fullscreen", fullScreen());

    resetSurfaces();
    setCursorState();

    myPendingRender = true;
  }
  else
    Logger::error("ERROR: Couldn't initialize video subsystem");

  // Restore sound settings
  myOSystem.sound().pause(oldPauseState);

  return status;
}
