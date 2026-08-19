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

#include "bspf.hxx"
#include "Logger.hxx"

#include "Console.hxx"
#include "EventHandler.hxx"
#include "OSystem.hxx"
#include "Settings.hxx"
#include "TIA.hxx"
#include "Sound.hxx"
#include "AudioSettings.hxx"
#include "MediaFactory.hxx"
#include "PNGLibrary.hxx"

#include "FBSurface.hxx"
#include "TIASurface.hxx"
#include "Bezel.hxx"
#include "MainFrameBuffer.hxx"
#include "StateManager.hxx"
#include "RewindManager.hxx"

#ifdef DEBUGGER_SUPPORT
  #include "Debugger.hxx"
#endif
#ifdef GUI_SUPPORT
  #include "Font.hxx"
  #include "StellaFont.hxx"
  #include "ConsoleMediumFont.hxx"
  #include "ConsoleMediumBFont.hxx"
  #include "StellaMediumFont.hxx"
  #include "StellaLargeFont.hxx"
  #include "Stella12x24tFont.hxx"
  #include "Stella14x28tFont.hxx"
  #include "Stella16x32tFont.hxx"
  #include "ConsoleFont.hxx"
  #include "Launcher.hxx"
  #include "DialogContainer.hxx"
  #include "TimeMachine.hxx"
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MainFrameBuffer::MainFrameBuffer(OSystem& osystem)
  : FrameBuffer(osystem)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MainFrameBuffer::~MainFrameBuffer() = default;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::initialize()
{
  FrameBuffer::initialize();

  // Create a TIA surface; we need it for rendering TIA images
  myTIASurface = std::make_unique<TIASurface>(myOSystem);
  // Create a bezel surface for TIA overlays
  myBezel = std::make_unique<Bezel>(myOSystem);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus MainFrameBuffer::createDisplay(string_view title, BufferType type,
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
    return FBInitStatus::FailTooLarge;
#else
  // Make sure this mode is even possible
  // We only really need to worry about it in non-windowed environments,
  // where requesting a window that's too large will probably cause a crash
  if(size > myDesktopSize[display])
    return FBInitStatus::FailTooLarge;
#endif

  if(myBufferType == BufferType::Emulator)
  {
    myBezel->load(); // make sure we have the correct bezel size

    // Determine possible TIA windowed zoom levels
    const auto currentTIAZoom =
      static_cast<double>(myOSystem.settings().getFloat("tia.zoom"));
    myOSystem.settings().setValue("tia.zoom",
      BSPF::clamp(currentTIAZoom, supportedTIAMinZoom(), supportedTIAMaxZoom()));
  }

  myMsgHandler.init();

  // Initialize video mode handler, so it can know what video modes are
  // appropriate for the requested image size
  myVidModeHandler.setImageSize(size);

  // Initialize video subsystem
  const string pre_about = myBackend->about();
  const FBInitStatus status = applyVideoMode();

  // Only set phosphor once when ROM is started
  if(myOSystem.eventHandler().inTIAMode())
  {
    // Phosphor mode can be enabled either globally or per-ROM
    int p_blend = 0;
    bool enable = false;
    const int phosphorMode = PhosphorHandler::toPhosphorMode(
      myOSystem.settings().getString(PhosphorHandler::SETTING_MODE));

    switch(phosphorMode)
    {
      case PhosphorHandler::Always:
        enable = true;
        p_blend = myOSystem.settings().getInt(PhosphorHandler::SETTING_BLEND);
        myOSystem.console().tia().enableAutoPhosphor(false);
        break;

      case PhosphorHandler::Auto_on:
      case PhosphorHandler::Auto:
        enable = false;
        p_blend = myOSystem.settings().getInt(PhosphorHandler::SETTING_BLEND);
        myOSystem.console().tia().enableAutoPhosphor(true, phosphorMode == PhosphorHandler::Auto_on);
        break;

      default: // PhosphorHandler::ByRom
        enable = myOSystem.console().properties().get(PropType::Display_Phosphor) == "YES";
        p_blend = BSPF::stoi(myOSystem.console().properties().get(PropType::Display_PPBlend));
        myOSystem.console().tia().enableAutoPhosphor(false);
        break;
    }
    myTIASurface->enablePhosphor(enable, p_blend);
  }

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
void MainFrameBuffer::update(UpdateMode mode)
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
  const bool rerender = (mode == UpdateMode::REDRAW || mode == UpdateMode::RERENDER
                         || myPendingRender);
  myPendingRender = false;

  // Show any messages enqueued from other threads (e.g. PlusROM/cart callbacks)
  myMsgHandler.drainPending();

  switch(myOSystem.eventHandler().state())
  {
    case EventHandlerState::NONE:
    case EventHandlerState::EMULATION:
      // Do nothing; emulation mode is handled separately (see below)
      return;

    case EventHandlerState::PAUSE:
    {
      // Show a pause message immediately and then every 7 seconds
      const bool shade = myOSystem.settings().getBool("pausedim");

      if(myMsgHandler.tickPause())
      {
        showTextMessage("Paused", MessagePosition::MiddleCenter);
        renderTIA(false, shade);
      }
      if(rerender)
        renderTIA(false, shade);
      break;  // EventHandlerState::PAUSE
    }

  #ifdef GUI_SUPPORT
    case EventHandlerState::OPTIONSMENU:
    case EventHandlerState::CMDMENU:
    case EventHandlerState::HIGHSCORESMENU:
    case EventHandlerState::MESSAGEMENU:
    case EventHandlerState::PLUSROMSMENU:
    case EventHandlerState::OVERLAYMENU:
    {
      // All GUI menus that overlay the TIA image share one render path;
      // the active DialogContainer is tracked by EventHandler::mainOverlay()
      DialogContainer& overlay = myOSystem.eventHandler().mainOverlay();
      overlay.tick();
      redraw |= overlay.needsRedraw();
      if(redraw)
      {
        renderTIA(true, true);
        overlay.draw(forceRedraw);
      }
      else if(rerender)
      {
        renderTIA(true, true);
        overlay.render();
      }
      break;  // GUI menu overlays
    }

    case EventHandlerState::TIMEMACHINE:
    {
      myOSystem.timeMachine().tick();
      redraw |= myOSystem.timeMachine().needsRedraw();
      if(redraw)
      {
        renderTIA();
        myOSystem.timeMachine().draw(forceRedraw);
      }
      else if(rerender)
      {
        renderTIA();
        myOSystem.timeMachine().render();
      }
      break;  // EventHandlerState::TIMEMACHINE
    }

    case EventHandlerState::PLAYBACK:
    {
      static Int32 frames = 0;
      bool success = true;

      if(--frames <= 0)
      {
        RewindManager& r = myOSystem.state().rewindManager();
        const uInt64 prevCycles = r.getCurrentCycles();

        success = r.unwindStates(1);

        // Determine playback speed, the faster the more the states are apart
        const Int64 frameCycles = static_cast<Int64>(76) * std::max<Int32>(myOSystem.console().tia().scanlinesLastFrame(), 240);
        const Int64 intervalFrames = r.getInterval() / frameCycles;
        const Int64 stateFrames = (r.getCurrentCycles() - prevCycles) / frameCycles;

        //frames = intervalFrames + std::sqrt(std::max(stateFrames - intervalFrames, 0));
        frames = std::round(std::sqrt(stateFrames));

        // Pause sound if saved states were removed or states are too far apart
        myOSystem.sound().pause(stateFrames > intervalFrames ||
            std::cmp_greater(frames, myOSystem.audioSettings().bufferSize() / 2 + 1));
      }
      redraw |= success;
      if(redraw)
        renderTIA(false);

      // Stop playback mode at the end of the state buffer
      // and switch to Time Machine or Pause mode
      if(!success)
      {
        frames = 0;
        myOSystem.sound().pause(true);
        myOSystem.eventHandler().enterMenuMode(EventHandlerState::TIMEMACHINE);
      }
      break;  // EventHandlerState::PLAYBACK
    }

    case EventHandlerState::LAUNCHER:
    {
      myOSystem.launcher().tick();
      redraw |= myOSystem.launcher().needsRedraw();
      if(redraw)
        myOSystem.launcher().draw(forceRedraw);
      else if(rerender)
        myOSystem.launcher().render();
      break;  // EventHandlerState::LAUNCHER
    }
  #endif  // GUI_SUPPORT

  #ifdef DEBUGGER_SUPPORT
    case EventHandlerState::DEBUGGER:
    {
      myOSystem.debugger().tick();
      redraw |= myOSystem.debugger().needsRedraw();
      if(redraw)
        myOSystem.debugger().draw(forceRedraw);
      else if(rerender)
        myOSystem.debugger().render();
      break;  // EventHandlerState::DEBUGGER
    }
  #endif  // DEBUGGER_SUPPORT
    default:
      break;
  }

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
void MainFrameBuffer::updateInEmulationMode(float framesPerSecond)
{
  // Update method that is specifically tailored to emulation mode
  //
  // We don't worry about selective rendering here; the rendering
  // always happens at the full framerate
  renderTIA();

  // Show any messages enqueued from the emulation worker thread (e.g. AR
  // Supercharger load notifications) before drawing them this frame
  myMsgHandler.drainPending();

  // Show frame statistics
  if(myMsgHandler.statsShown())
    myMsgHandler.drawStats(framesPerSecond);

  myMsgHandler.onEmulationFrame();

  // Draw any pending messages
  if(myMsgHandler.isShown())
    myMsgHandler.draw();

  // Push buffers to screen
  myBackend->renderToScreen();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::enableMessages(bool enable)
{
  myMsgHandler.enable(enable);
  if(!enable)
  {
    // Update immediately
    if(myOSystem.eventHandler().state() == EventHandlerState::EMULATION)
      renderTIA();
    else
      update();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::renderTIA(bool doClear, bool shade)
{
  if(doClear)
    clear();  // TODO - test this: it may cause slowdowns on older systems

  myTIASurface->render(shade);
  if(myBezel)
    myBezel->render();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::setTIAPalette(const PaletteArray& rgb_palette)
{
  // Hoist shift values — surface format is constant
  const uInt32 rShift = std::countr_zero(rMask());
  const uInt32 gShift = std::countr_zero(gMask());
  const uInt32 bShift = std::countr_zero(bMask());
  const uInt32 aMask_ = aMask();  // fully transparent; no alpha in RGB palette

  // Create a TIA palette from the raw RGB data
  PaletteArray tia_palette = {0};
  for(int i = 0; i < 256; ++i)
  {
    const uInt32 rgb = rgb_palette[i];
    tia_palette[i] = aMask_
                   | (((rgb >> 16) & 0xFF) << rShift)
                   | (((rgb >>  8) & 0xFF) << gShift)
                   | (( rgb        & 0xFF) << bShift);
  }
  // Remember the TIA palette; place it at the beginning of the full palette
  std::copy_n(tia_palette.begin(), tia_palette.size(), myFullPalette.begin());

  // Let the TIA surface know about the new palette
  myTIASurface->setPalette(tia_palette, rgb_palette);

  // Since the UI palette shares the TIA palette, we need to update it too
  setUIPalette();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::setDisasmPalette()
{
  const Settings& settings = myOSystem.settings();
  const string& key = settings.getBool("altuipalette") ? "uipalette2" : "uipalette";
  const string& name = settings.getString(key);
  const bool isDark = (name == "dark" || name == "classic");
  const DisasmPaletteArray& dp = isDark ? ourDarkDisasmPalette : ourStandardDisasmPalette;

  const uInt32 rShift = std::countr_zero(rMask());
  const uInt32 gShift = std::countr_zero(gMask());
  const uInt32 bShift = std::countr_zero(bMask());
  const uInt32 aMask_ = aMask();

  for(auto i = 0UZ; i < dp.size(); ++i)
  {
    const uInt32 rgb = dp[i];
    myFullPalette[kUINColors + i] = aMask_
                                  | (((rgb >> 16) & 0xFF) << rShift)
                                  | (((rgb >>  8) & 0xFF) << gShift)
                                  | (( rgb        & 0xFF) << bShift);
  }
  FBSurface::setPalette(myFullPalette);
}

#ifdef ADAPTABLE_REFRESH_SUPPORT
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::toggleAdaptRefresh(bool toggle)
{
  bool isAdaptRefresh = myOSystem.settings().getInt("tia.fs_refresh");

  if(toggle)
    isAdaptRefresh = !isAdaptRefresh;

  if(myBufferType == BufferType::Emulator)
  {
    if(toggle)
    {
      myOSystem.settings().setValue("tia.fs_refresh", isAdaptRefresh);
      // issue a complete framebuffer re-initialization
      myOSystem.createFrameBuffer();
    }

    showTextMessage(std::format("Adapt refresh rate {} ({} Hz)",
      isAdaptRefresh ? "enabled" : "disabled",
      myBackend->refreshRate()));
  }
}
#endif  // ADAPTABLE_REFRESH_SUPPORT

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::changeOverscan(int direction)
{
  if(fullScreen())
  {
    const int oldOverscan = myOSystem.settings().getInt("tia.fs_overscan");
    const int overscan = BSPF::clamp(oldOverscan + direction, 0, 10);

    if(overscan != oldOverscan)
    {
      myOSystem.settings().setValue("tia.fs_overscan", overscan);

      // issue a complete framebuffer re-initialization
      myOSystem.createFrameBuffer();
    }

    const string val = overscan
      ? std::format("{}{}{}", overscan > 0 ? "+" : "", overscan, "%")
      : "Off";
    myOSystem.frameBuffer().showGaugeMessage("Overscan", val, overscan, 0, 10);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::switchVideoMode(int direction)
{
  // Only applicable when in TIA/emulation mode
  if(!myOSystem.eventHandler().inTIAMode())
    return;

  if(!fullScreen())
  {
    // Windowed TIA modes support variable zoom levels
    auto zoom = static_cast<double>(myOSystem.settings().getFloat("tia.zoom"));
    if(direction == +1)       zoom += ZOOM_STEPS;
    else if(direction == -1)  zoom -= ZOOM_STEPS;

    // Make sure the level is within the allowable desktop size
    zoom = BSPF::clampw(zoom, supportedTIAMinZoom(), supportedTIAMaxZoom());
    myOSystem.settings().setValue("tia.zoom", zoom);
  }
  else
  {
    // In fullscreen mode, there are only two modes, so direction
    // is irrelevant
    if(direction == +1 || direction == -1)
    {
      const bool stretch = myOSystem.settings().getBool("tia.fs_stretch");
      myOSystem.settings().setValue("tia.fs_stretch", !stretch);
    }
  }

  saveCurrentWindowPosition();
  if(!direction || applyVideoMode() == FBInitStatus::Success)
  {
    if(fullScreen())
      showTextMessage(myActiveVidMode.description);
    else
      showGaugeMessage("Zoom", myActiveVidMode.description,
                       static_cast<float>(myActiveVidMode.zoom),
                       static_cast<float>(supportedTIAMinZoom()),
                       static_cast<float>(supportedTIAMaxZoom()));
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::toggleBezel(bool toggle)
{
  bool enabled = myOSystem.settings().getBool("bezel.show");

  if(toggle && myBufferType == BufferType::Emulator)
  {
    if(!fullScreen() && !myOSystem.settings().getBool("bezel.windowed"))
    {
      myOSystem.frameBuffer().showTextMessage("Bezels in windowed mode are not enabled");
      return;
    }
    else
    {
      enabled = !enabled;
      myOSystem.settings().setValue("bezel.show", enabled);
      if(!myBezel->load() && enabled)
      {
        myOSystem.settings().setValue("bezel.show", !enabled);
        return;
      }
      else
      {
        // Determine possible TIA windowed zoom levels
        const auto currentTIAZoom =
          static_cast<double>(myOSystem.settings().getFloat("tia.zoom"));
        myOSystem.settings().setValue("tia.zoom",
          BSPF::clamp(currentTIAZoom, supportedTIAMinZoom(), supportedTIAMaxZoom()));

        saveCurrentWindowPosition();
        applyVideoMode();
      }
    }
  }
  myOSystem.frameBuffer().showTextMessage(enabled ? "Bezel enabled" : "Bezel disabled");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus MainFrameBuffer::applyVideoMode()
{
  // Update display size, in case windowed/fullscreen mode has changed
  const Settings& s = myOSystem.settings();
  const int ID = displayId(); // TODO SDL 3:

  if(s.getBool("fullscreen"))
    myVidModeHandler.setDisplaySize(myFullscreenDisplays[ID], true);
  else
    myVidModeHandler.setDisplaySize(myAbsDesktopSize[ID], false);

  const bool inTIAMode = myOSystem.eventHandler().inTIAMode();

#ifdef IMAGE_SUPPORT
  if(inTIAMode)
    myBezel->load();
#endif

  // Build the new mode based on current settings
  const VideoModeHandler::Mode& mode
    = myVidModeHandler.buildMode(s, inTIAMode, myBezel->info());
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

    // Inform TIA surface about new mode, and update TIA settings
    if(inTIAMode)
    {
      myTIASurface->initialize(myOSystem.console(), myActiveVidMode);
      if(fullScreen())
        myOSystem.settings().setValue("tia.fs_stretch",
          myActiveVidMode.stretch == VideoModeHandler::Mode::Stretch::Fill);
      else
        myOSystem.settings().setValue("tia.zoom", myActiveVidMode.zoom);

      myBezel->apply();
    }

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


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::setUIPalette()
{
  FrameBuffer::setUIPalette();
  setDisasmPalette();  // fills disasm slots and calls FBSurface::setPalette
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
double MainFrameBuffer::maxWindowZoom() const
{
  const uInt32 display = displayId(BufferType::Emulator);
  double multiplier = 1;

  for(;;)
  {
    // Figure out the zoomed size of the window (incl. the bezel)
    const uInt32 width  = static_cast<double>(TIAConstants::viewableWidth)  * myBezel->ratioW() * multiplier;
    const uInt32 height = static_cast<double>(TIAConstants::viewableHeight) * myBezel->ratioH() * multiplier;

    if((width > myAbsDesktopSize.at(display).w) ||
       (height > myAbsDesktopSize.at(display).h))
      break;

    multiplier += ZOOM_STEPS;
  }
  return multiplier > 1 ? multiplier - ZOOM_STEPS : 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MainFrameBuffer::setMinZoom(const FontDesc &fd)
{

    // Determine minimal zoom level based on the default font
    //  So what fits with default font should fit for any font.
    //  However, we have to make sure all Dialogs are sized using the fontsize.
    const int zoom_h = (fd.height * 4 * 2) / GUI::stellaMediumDesc.height;
    const int zoom_w = (fd.maxwidth * 4 * 2) / GUI::stellaMediumDesc.maxwidth;
    // round to 25% steps, >= 200%
    myTIAMinZoom = std::max(std::max(zoom_w, zoom_h) / 4., 2.);

}

// Disassembly palettes — entry order matches kDisasmBlack..kDisasmWhite
// "standard": muted shades readable on light UI backgrounds (Standard, Light)
DisasmPaletteArray MainFrameBuffer::ourStandardDisasmPalette = {{
  0x202020,  // Black
  0xbb1100,  // Red
  0xcc5500,  // Orange
  0xaa7700,  // Yellow  (amber)
  0x558800,  // Lime
  0x226600,  // Green
  0x006666,  // Teal
  0x007799,  // Cyan
  0x2255cc,  // Blue
  0x333399,  // Indigo
  0x6633aa,  // Violet
  0x882288,  // Magenta
  0xaa2266,  // Pink
  0x774422,  // Brown
  0x666666,  // Gray
  0xf0f0f0,  // White
}};
// "dark": vivid shades readable on dark UI backgrounds (Classic, Dark)
DisasmPaletteArray MainFrameBuffer::ourDarkDisasmPalette = {{
  0x101010,  // Black
  0xff6060,  // Red
  0xff9944,  // Orange
  0xffdd00,  // Yellow
  0xaaff44,  // Lime
  0x44dd44,  // Green
  0x22ddbb,  // Teal
  0x44ddff,  // Cyan
  0x6699ff,  // Blue
  0x8888ff,  // Indigo
  0xbb77ff,  // Violet
  0xff66ff,  // Magenta
  0xff66aa,  // Pink
  0xcc8855,  // Brown
  0xaaaaaa,  // Gray
  0xffffff,  // White
}};
