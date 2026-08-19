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
#include "MediaFactory.hxx"
#include "PNGLibrary.hxx"

#include "FBSurface.hxx"
#include "FrameBuffer.hxx"
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
FrameBuffer::FrameBuffer(OSystem& osystem)
  : myOSystem{osystem},
    myMsgHandler{*this, osystem}
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FrameBuffer::~FrameBuffer() = default;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::initialize()
{
  // First create the platform-specific backend; it is needed before anything
  // else can be used
  myBackend = MediaFactory::createVideoBackend(myOSystem, *this);

  // Get desktop resolution and supported renderers
  myBackend->queryHardware(myFullscreenDisplays, myWindowedDisplays, myRenderers);

  for(const auto& display: myWindowedDisplays)
  {
    uInt32 query_w = display.second.w, query_h = display.second.h;

    // Check the 'maxres' setting, which is an undocumented developer feature
    // that specifies the desktop size (not normally set)
    const Common::Size& s = myOSystem.settings().getSize("maxres");
    if(s.valid())
    {
      query_w = s.w;
      query_h = s.h;
    }
    // Various parts of the codebase assume a minimum screen size
    Common::Size size(std::max(query_w, FBMinimum::Width), std::max(query_h, FBMinimum::Height));
    myAbsDesktopSize[display.first] = size;

    // Check for HiDPI mode (is it activated, and can we use it?)
    const bool hidpi = (((size.w / 2) >= FBMinimum::Width) &&
                        ((size.h / 2) >= FBMinimum::Height));
    myHiDPIAllowed[display.first] = hidpi;
    myHiDPIEnabled[display.first] = hidpi && myOSystem.settings().getBool("hidpi");

    // In HiDPI mode, the desktop resolution is essentially halved
    // Later, the output is scaled and rendered in 2x mode
    if(myHiDPIEnabled[display.first])
    {
      size.w /= hidpiScaleFactor();
      size.h /= hidpiScaleFactor();
    }
    myDesktopSize[display.first] = size;
  }

#ifdef GUI_SUPPORT
  setupFonts();
#endif

  updateTheme();
  setUIPalette();

  myGrabMouse = myOSystem.settings().getBool("grabmouse");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 FrameBuffer::displayId(BufferType bufferType) const
{
  uInt32 display = 0;

  if(bufferType == myBufferType || bufferType == BufferType::None)
    display = myBackend->getCurrentDisplayID();
  else
    display = myOSystem.settings().getInt(
      getDisplayKey(bufferType != BufferType::None
        ? bufferType
        : myBufferType)
    );

  // If the requested display ID is not available, default to the first one
  // in the container (normally the primary display)
  if(!myWindowedDisplays.contains(display))
    display = myWindowedDisplays.begin()->first;

  return display;
}

#ifdef GUI_SUPPORT
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::setupFonts()
{
  ////////////////////////////////////////////////////////////////////
  // Create fonts to draw text
  // NOTE: the logic determining appropriate font sizes is done here,
  //       so that the UI classes can just use the font they expect,
  //       and not worry about it
  //       This logic should also take into account the size of the
  //       framebuffer, and try to be intelligent about font sizes
  //       We can probably add ifdefs to take care of corner cases,
  //       but that means we've failed to abstract it enough ...
  ////////////////////////////////////////////////////////////////////

  // This font is used in a variety of situations when a really small
  // font is needed; we let the specific widget/dialog decide when to
  // use it
  mySmallFont = std::make_unique<GUI::Font>(GUI::stellaDesc); // 6x10

  if(myOSystem.settings().getBool("minimal_ui"))
  {
    // The general font used in all UI elements
    myFont = std::make_unique<GUI::Font>(GUI::stella12x24tDesc);           // 12x24
    // The info font used in all UI elements
    myInfoFont = std::make_unique<GUI::Font>(GUI::stellaLargeDesc);        // 10x20
  }
  else
  {
    constexpr int NUM_FONTS = 7;
    const FontDesc FONT_DESC[NUM_FONTS] = {
      GUI::consoleDesc, GUI::consoleMediumDesc, GUI::stellaMediumDesc,
      GUI::stellaLargeDesc, GUI::stella12x24tDesc, GUI::stella14x28tDesc,
      GUI::stella16x32tDesc};
    const string_view dialogFont = myOSystem.settings().getString("dialogfont");
    const FontDesc fd = getFontDesc(dialogFont);

    // The general font used in all UI elements
    myFont = std::make_unique<GUI::Font>(fd);                                //  default: 9x18
    // The info font used in all UI elements,
    //  automatically determined aiming for 1 / 1.4 (~= 18 / 13) size
    int fontIdx = 0;
    for(int i = 0; i < NUM_FONTS; ++i)
    {
      if(fd.height <= FONT_DESC[i].height * 1.4)
      {
        fontIdx = i;
        break;
      }
    }
    myInfoFont = std::make_unique<GUI::Font>(FONT_DESC[fontIdx]);            //  default 8x13

    // Determine minimal zoom level based on the default font
    setMinZoom(fd);
  }

  // The font used by the ROM launcher
  const string_view lf = myOSystem.settings().getString("launcherfont");

  myLauncherFont = std::make_unique<GUI::Font>(getFontDesc(lf));       //  8x13
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FontDesc FrameBuffer::getFontDesc(string_view name)
{
  if(name == "small")
    return GUI::consoleDesc;        //  8x13
  else if(name == "low_medium")
    return GUI::consoleMediumBDesc; //  9x15
  else if(name == "medium")
    return GUI::stellaMediumDesc;   //  9x18
  else if(name == "large" || name == "large10")
    return GUI::stellaLargeDesc;    // 10x20
  else if(name == "large12")
    return GUI::stella12x24tDesc;   // 12x24
  else if(name == "large14")
    return GUI::stella14x28tDesc;   // 14x28
  else // "large16"
    return GUI::stella16x32tDesc;   // 16x32
}
#endif  // GUI_SUPPORT

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::showTextMessage(string_view message,
                                  MessagePosition position, bool force)
{
#ifdef GUI_SUPPORT
  myMsgHandler.showText(message, position, force);
#else
  if(myBackend && (force || myOSystem.settings().getBool("uimessages")))
    myBackend->showMessage(message);
#endif  // GUI_SUPPORT
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::showGaugeMessage(string_view message, string_view valueText,
                                   float value, float minValue, float maxValue)
{
#ifdef GUI_SUPPORT
  myMsgHandler.showGauge(message, valueText, value, minValue, maxValue);
#else
  if(myBackend && (myOSystem.settings().getBool("uimessages")))
    myBackend->showGaugeMessage(message, valueText, value, minValue, maxValue);
#endif  // GUI_SUPPORT
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FrameBuffer::messageShown() const
{
  return myMsgHandler.isShown();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::toggleFrameStats(bool toggle)
{
  if(toggle)
    myMsgHandler.showStats(!myMsgHandler.statsEnabled());
  myOSystem.settings().setValue(
    myOSystem.settings().getBool("dev.settings") ? "dev.stats" : "plr.stats",
    myMsgHandler.statsEnabled());

  showTextMessage(std::format("Console info {}",
    myMsgHandler.statsEnabled() ? "enabled" : "disabled"));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::showFrameStats(bool enable)
{
  myMsgHandler.showStats(enable);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::enableMessages(bool enable)
{
  myMsgHandler.enable(enable);
  if(!enable)
  {
    // Update immediately
    update();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::setPauseDelay()
{
  myMsgHandler.setPauseDelay();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
shared_ptr<FBSurface> FrameBuffer::allocateSurface(
    int w, int h, ScalingInterpolation inter, const uInt32* data)
{
  mySurfaceList.push_back(myBackend->createSurface(w, h, inter, data));
  return mySurfaceList.back();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::deallocateSurface(const shared_ptr<FBSurface>& surface)
{
  if(surface)
    mySurfaceList.remove(surface);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::resetSurfaces()
{
  for(auto& surface: mySurfaceList)
    surface->reload();

  update(UpdateMode::REDRAW); // force full update
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::setUIPalette()
{
  const Settings& settings = myOSystem.settings();
  const string& key = settings.getBool("altuipalette") ? "uipalette2" : "uipalette";
  // Set palette for UI (upper area of full palette)
  const UIPaletteArray& ui_palette =
     (settings.getString(key) == "classic") ? ourClassicUIPalette :
     (settings.getString(key) == "light")   ? ourLightUIPalette :
     (settings.getString(key) == "dark")    ? ourDarkUIPalette :
      ourStandardUIPalette;

  // Hoist shift values — surface format is constant
  const uInt32 rShift = std::countr_zero(rMask());
  const uInt32 gShift = std::countr_zero(gMask());
  const uInt32 bShift = std::countr_zero(bMask());
  const uInt32 aMask_ = aMask();

  for(auto i = 0UZ; i < ui_palette.size(); ++i)
  {
    const uInt32 rgb = ui_palette[i];
    myFullPalette[kColor + i] = aMask_
                              | (((rgb >> 16) & 0xFF) << rShift)
                              | (((rgb >>  8) & 0xFF) << gShift)
                              | (( rgb        & 0xFF) << bShift);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::stateChanged(EventHandlerState state)
{
  // Prevent removing state change messages (brand-new ones survive transitions)
  if(!myMsgHandler.msgJustShown())
    myMsgHandler.hide();
  update(); // update immediately
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string FrameBuffer::getDisplayKey(BufferType bufferType) const
{
  if(bufferType == BufferType::None)
    bufferType = myBufferType;

  // save current window's display and position
  switch(bufferType)
  {
    case BufferType::Launcher:
      return "launcherdisplay";

    case BufferType::Emulator:
      return "display";

    #ifdef DEBUGGER_SUPPORT
    case BufferType::Debugger:
      return "dbg.display";
    #endif

    #ifdef MEMVIEW_SUPPORT
    case BufferType::MemView:
      return "mv.display";
    #endif

    default:
      return "";
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string FrameBuffer::getPositionKey() const
{
  // save current window's display and position
  switch(myBufferType)
  {
    case BufferType::Launcher:
      return "launcherpos";

    case BufferType::Emulator:
      return  "windowedpos";

    #ifdef DEBUGGER_SUPPORT
    case BufferType::Debugger:
      return "dbg.pos";
    #endif

    #ifdef MEMVIEW_SUPPORT
    case BufferType::MemView:
      return "mv.pos";
    #endif

    default:
      return "";
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::saveCurrentWindowPosition() const
{
  if(myBackend)
  {
    myOSystem.settings().setValue(
      getDisplayKey(), myBackend->getCurrentDisplayID());
    if(myBackend->isCurrentWindowPositioned())
      myOSystem.settings().setValue(
        getPositionKey(), myBackend->getCurrentWindowPos());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::saveConfig(Settings& settings) const
{
  // Save the last windowed position and display on system shutdown
  saveCurrentWindowPosition();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::setFullscreen(bool enable)
{
#ifdef WINDOWED_SUPPORT
  // Switching between fullscreen and windowed modes will invariably mean
  // that the 'window' resolution changes.  Currently, dialogs are not
  // able to resize themselves when they are actively being shown
  // (they would have to be closed and then re-opened, etc).
  // For now, we simply disallow screen switches in such modes
  switch(myOSystem.eventHandler().state())
  {
    case EventHandlerState::EMULATION:
    case EventHandlerState::PAUSE:
      break; // continue with processing (aka, allow a mode switch)
    case EventHandlerState::DEBUGGER:
    case EventHandlerState::LAUNCHER:
      if(myOSystem.eventHandler().mainOverlay().baseDialogIsActive())
        break; // allow a mode switch when there is only one dialog
      [[fallthrough]];
    default:
      return;
  }

  myOSystem.settings().setValue("fullscreen", enable);
  saveCurrentWindowPosition();
  applyVideoMode();
#endif  // WINDOWED_SUPPORT
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::toggleFullscreen(bool toggle)
{
  const EventHandlerState state = myOSystem.eventHandler().state();

  switch(state)
  {
    case EventHandlerState::LAUNCHER:
    case EventHandlerState::EMULATION:
    case EventHandlerState::PAUSE:
    case EventHandlerState::DEBUGGER:
    {
      const bool isFullscreen = toggle ? !fullScreen() : fullScreen();
      setFullscreen(isFullscreen);

      if(state != EventHandlerState::LAUNCHER)
      {
        const string_view state_str = isFullscreen ? "enabled" : "disabled";

        if(state != EventHandlerState::DEBUGGER)
        {
          const string msg = isFullscreen
            ? std::format("Fullscreen {} ({} Hz, Zoom {}%)",
                state_str, myBackend->refreshRate(),
                static_cast<int>(round(myActiveVidMode.zoom * 100)))
            : std::format("Fullscreen {} (Zoom {}%)",
                state_str,
                static_cast<int>(round(myActiveVidMode.zoom * 100)));
          showTextMessage(msg);
        }
        else
          showTextMessage(std::format("Fullscreen {}", state_str));
      }
      break;
    }
    default:
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::setCursorState()
{
  myGrabMouse = myOSystem.settings().getBool("grabmouse");
  // Always grab mouse in emulation (if enabled) and emulating a controller
  // that always uses the mouse
  const bool emulation =
      myOSystem.eventHandler().state() == EventHandlerState::EMULATION;
  const bool usesLightgun = emulation && myOSystem.hasConsole() ?
    myOSystem.console().leftController().type() == Controller::Type::Lightgun ||
    myOSystem.console().rightController().type() == Controller::Type::Lightgun : false;
  // Show/hide cursor in UI/emulation mode based on 'cursor' setting
  int cursor = myOSystem.settings().getInt("cursor");

  // Always enable cursor in lightgun games
  if (usesLightgun && !myGrabMouse)
    cursor |= 1;  // +Emulation

  switch(cursor)
  {
    case 0:                   // -UI, -Emulation
      showCursor(false);
      break;
    case 1:
      showCursor(emulation);  // -UI, +Emulation
      break;
    case 2:                   // +UI, -Emulation
      showCursor(!emulation);
      break;
    case 3:
      showCursor(true);       // +UI, +Emulation
      break;
    default:
      break;
  }

  myGrabMouse &= grabMouseAllowed();
  myBackend->grabMouse(myGrabMouse);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::enableTextEvents(bool enable)
{
  myBackend->enableTextEvents(enable);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FrameBuffer::grabMouseAllowed()
{
  // Allow grabbing mouse in emulation (if enabled) and emulating a controller
  // that always uses the mouse
  const bool emulation =
    myOSystem.eventHandler().state() == EventHandlerState::EMULATION;
  const bool analog = myOSystem.hasConsole() ?
    (myOSystem.console().leftController().usesMouse() ||
     myOSystem.console().rightController().usesMouse()) : false;
  const bool usesLightgun = emulation && myOSystem.hasConsole() ?
    myOSystem.console().leftController().type() == Controller::Type::Lightgun ||
    myOSystem.console().rightController().type() == Controller::Type::Lightgun : false;
  const bool alwaysUseMouse = BSPF::equalsIgnoreCase("always", myOSystem.settings().getString("usemouse"));

  // Disable grab while cursor is shown in emulation
  const bool cursorHidden = !(myOSystem.settings().getInt("cursor") & 1);

  return emulation && (analog || usesLightgun || alwaysUseMouse) && cursorHidden;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::enableGrabMouse(bool enable)
{
  myGrabMouse = enable;
  setCursorState();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void FrameBuffer::toggleGrabMouse(bool toggle)
{
  bool oldState = myGrabMouse = myOSystem.settings().getBool("grabmouse");

  if(toggle)
  {
    if(grabMouseAllowed())
    {
      myGrabMouse = !myGrabMouse;
      myOSystem.settings().setValue("grabmouse", myGrabMouse);
      setCursorState();
    }
  }
  else
    oldState = !myGrabMouse; // display current state

  myOSystem.frameBuffer().showTextMessage(oldState != myGrabMouse ? myGrabMouse
                                          ? "Grab mouse enabled" : "Grab mouse disabled"
                                          : "Grab mouse not allowed");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FrameBuffer::updateTheme()
{
  if(myOSystem.settings().getBool("autouipalette"))
  {
    const bool darkTheme = myOSystem.settings().getBool("altuipalette");

    if((myBackend->isLightTheme() && darkTheme) ||
       (myBackend->isDarkTheme() && !darkTheme))
    {
      myOSystem.settings().setValue("altuipalette", !darkTheme);
      return true;
    }
  }
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 FrameBuffer::windowId(BufferType bufferType) const
{
  return myBackend->getCurrentWindowID();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
/*
  Palette is defined as follows:
    *** Base colors ***
    kColor            Normal foreground color (non-text)
    kBGColor          Normal background color (non-text)
    kBGColorLo        Disabled background color dark (non-text)
    kBGColorHi        Disabled background color light (non-text)
    kShadowColor      Item is disabled (unused)
    *** Text colors ***
    kTextColor        Normal text color
    kTextColorHi      Highlighted text color
    kTextColorEm      Emphasized text color
    kTextColorInv     Color for selected text
    kTextColorLink    Color for links
    *** UI elements (dialog and widgets) ***
    kDlgColor         Dialog background
    kWidColor         Widget background
    kWidColorHi       Widget highlight color
    kWidFrameColor    Border for currently selected widget
    *** Button colors ***
    kBtnColor         Normal button background
    kBtnColorHi       Highlighted button background
    kBtnBorderColor,
    kBtnBorderColorHi,
    kBtnTextColor     Normal button font color
    kBtnTextColorHi   Highlighted button font color
    *** Checkbox colors ***
    kCheckColor       Color of 'X' in checkbox
    *** Scrollbar colors ***
    kScrollColor      Normal scrollbar color
    kScrollColorHi    Highlighted scrollbar color
    *** Debugger colors ***
    kDbgChangedColor      Background color for changed cells
    kDbgChangedTextColor  Text color for changed cells
    kDbgColorHi           Highlighted color in debugger data cells
    kDbgColorRed          Red color in debugger
    *** Slider colors ***
    kSliderColor          Enabled slider
    kSliderColorHi        Focussed slider
    kSliderBGColor        Enabled slider background
    kSliderBGColorHi      Focussed slider background
    kSliderBGColorLo      Disabled slider background
    *** Other colors ***
    kColorInfo            TIA output position color
    kColorTitleBar        Title bar color
    kColorTitleText       Title text color
*/
UIPaletteArray FrameBuffer::ourStandardUIPalette = {
  { 0x686868, 0x000000, 0xa38c61, 0xdccfa5, 0x404040,           // base
    0x000000, 0xac3410, 0x9f0000, 0xf0f0cf, 0xac3410,           // text
    0xc9af7c, 0xf0f0cf, 0xd55941, 0xc80000,                     // UI elements
    0xac3410, 0xd55941, 0x686868, 0xdccfa5, 0xf0f0cf, 0xf0f0cf, // buttons
    0xac3410,                                                   // checkbox
    0xac3410, 0xd55941,                                         // scrollbar
    0xc80000, 0xffff80, 0xc8c8ff, 0xc80000,                     // debugger
    0xac3410, 0xd55941, 0xdccfa5, 0xf0f0cf, 0xa38c61,           // slider
    0xffffff, 0xac3410, 0xf0f0cf                                // other
  }
};

UIPaletteArray FrameBuffer::ourClassicUIPalette = {
  { 0x686868, 0x000000, 0x404040, 0x404040, 0x404040,           // base
    0x20a020, 0x00ff00, 0xc80000, 0x000000, 0x00ff00,           // text
    0x000000, 0x000000, 0x00ff00, 0xc80000,                     // UI elements
    0x000000, 0x000000, 0x686868, 0x00ff00, 0x20a020, 0x00ff00, // buttons
    0x20a020,                                                   // checkbox
    0x20a020, 0x00ff00,                                         // scrollbar
    0xc80000, 0x00ff00, 0xc8c8ff, 0xc80000,                     // debugger
    0x20a020, 0x00ff00, 0x404040, 0x686868, 0x404040,           // slider
    0x00ff00, 0x20a020, 0x000000                                // other
  }
};

UIPaletteArray FrameBuffer::ourLightUIPalette = {
  { 0x808080, 0x000000, 0xc0c0c0, 0xe1e1e1, 0x333333,           // base
    0x000000, 0xBDDEF9, 0x0078d7, 0x000000, 0x005aa1,           // text
    0xf0f0f0, 0xffffff, 0x0078d7, 0x0f0f0f,                     // UI elements
    0xe1e1e1, 0xe5f1fb, 0x808080, 0x0078d7, 0x000000, 0x000000, // buttons
    0x333333,                                                   // checkbox
    0xc0c0c0, 0x808080,                                         // scrollbar
    0xffc0c0, 0x000000, 0xe00000, 0xc00000,                     // debugger
    0x333333, 0x0078d7, 0xc0c0c0, 0xffffff, 0xc0c0c0,           // slider 0xBDDEF9| 0xe1e1e1 | 0xffffff
    0xffffff, 0x333333, 0xf0f0f0                                // other
  }
};

UIPaletteArray FrameBuffer::ourDarkUIPalette = {
  { 0x646464, 0xc0c0c0, 0x3c3c3c, 0x282828, 0x989898,           // base
    0xc0c0c0, 0x1567a5, 0x0064b7, 0xc0c0c0, 0x1d92e0,           // text
    0x202020, 0x000000, 0x0059a3, 0xb0b0b0,                     // UI elements
    0x282828, 0x00467f, 0x646464, 0x0059a3, 0xc0c0c0, 0xc0c0c0, // buttons
    0x989898,                                                   // checkbox
    0x3c3c3c, 0x646464,                                         // scrollbar
    0x7f2020, 0xc0c0c0, 0xe00000, 0xc00000,                     // debugger
    0x989898, 0x0059a3, 0x3c3c3c, 0x000000, 0x3c3c3c,           // slider
    0x000000, 0x404040, 0xc0c0c0                                // other
  }
};
