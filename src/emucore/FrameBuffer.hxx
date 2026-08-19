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

#ifndef FRAME_BUFFER_HXX
#define FRAME_BUFFER_HXX

#include <list>
#include <unordered_map>

class OSystem;
class Console;
class Settings;
class FBSurface;
class Bezel;

#ifdef GUI_SUPPORT
  #include "Font.hxx"
#endif

#include "Rect.hxx"
#include "Variant.hxx"
#include "FBBackend.hxx"
#include "FBMessageHandler.hxx"
#include "FrameBufferConstants.hxx"
#include "EventHandlerConstants.hxx"
#include "VideoModeHandler.hxx"
#include "bspf.hxx"

/**
  This class encapsulates all video buffers and is the basis for the video
  display in Stella.  The FBBackend object contained in this class is
  platform-specific, and most rendering tasks are delegated to it.

  The TIA is drawn here, and all GUI elements (ala ScummVM, which are drawn
  into FBSurfaces), are in turn drawn here as well.

  @author  Stephen Anthony
*/
class FrameBuffer
{
  public:
    // Zoom level step interval
    static constexpr double ZOOM_STEPS = 0.25;

  public:
    enum class UpdateMode: uInt8 {
      NONE = 0,
      REDRAW = 1,
      RERENDER = 2
    };

  public:
    explicit FrameBuffer(OSystem& osystem);
    virtual ~FrameBuffer();

    /**
      Initialize the framebuffer object (set up the underlying hardware).
      Throws an exception upon encountering any errors.
    */
    virtual void initialize();

    /**
      (Re)creates the framebuffer display.  This must be called before any
      calls are made to derived methods.

      @param title   The title of the application / window
      @param size    The dimensions of the display
      @param honourHiDPI  If true, consult the 'hidpi' setting and enlarge
                          the display size accordingly; if false, use the
                          exact dimensions as given

      @return  Status of initialization (see FBInitStatus 'enum')
    */
    virtual FBInitStatus createDisplay(string_view title, BufferType type,
                               Common::Size size, bool honourHiDPI = true) = 0;

    /**
      Updates the display, which depending on the current mode could mean
      drawing the TIA, any pending menus, etc.
    */
    virtual void update(UpdateMode mode = UpdateMode::NONE) = 0;

    /**
      Set pending rendering flag.
    */
    void setPendingRender() { myPendingRender = true; }

    /**
      Shows a text message onscreen.

      @param message  The message to be shown
      @param position Onscreen position for the message
      @param force    Force showing this message, even if messages are disabled
    */
    void showTextMessage(string_view message,
                         MessagePosition position = MessagePosition::BottomCenter,
                         bool force = false);
    /**
      Shows a message with a gauge bar onscreen.

      @param message    The message to be shown
      @param valueText  The value of the gauge bar as text
      @param value      The gauge bar percentage
      @param minValue   The minimal value of the gauge bar
      @param maxValue   The maximal value of the gauge bar
    */
    void showGaugeMessage(string_view message, string_view valueText,
                          float value, float minValue = 0.F, float maxValue = 100.F);

    bool messageShown() const;

    /**
      Toggles showing or hiding framerate statistics.
    */
    void toggleFrameStats(bool toggle = true);

    /**
      Shows a message containing frame statistics for the current frame.
    */
    void showFrameStats(bool enable);

    /**
      Enable/disable any pending messages.  Disabled messages aren't removed
      from the message queue; they're just not redrawn into the framebuffer.
    */
    virtual void enableMessages(bool enable);

    /**
      Reset 'Paused' display delay counter
    */
    void setPauseDelay();

    /**
      Allocate a new surface.  The FrameBuffer class takes all responsibility
      for freeing this surface (ie, other classes must not delete it directly).

      @param w      The requested width of the new surface
      @param h      The requested height of the new surface
      @param inter  Interpolation mode
      @param data   If non-null, use the given data values as a static surface

      @return  A pointer to a valid surface object, or nullptr
    */
    shared_ptr<FBSurface> allocateSurface(
      int w,
      int h,
      ScalingInterpolation inter = ScalingInterpolation::none,
      const uInt32* data = nullptr
    );

    /**
      Deallocate a previously allocated surface.  If no such surface exists,
      this method does nothing.

      @param surface  The surface to remove/deallocate
    */
    void deallocateSurface(const shared_ptr<FBSurface>& surface);

    /**
      Set palette for user interface.
    */
    virtual void setUIPalette();

    /**
      Returns the current dimensions of the framebuffer image.
      Note that this will take into account the current scaling (if any)
      as well as image 'centering'.
    */
    const Common::Rect& imageRect() const { return myActiveVidMode.imageR; }

    /**
      Returns the current dimensions of the framebuffer window.
      This is the entire area containing the framebuffer image as well as any
      'unusable' area.
    */
    const Common::Size& screenSize() const { return myActiveVidMode.screenS; }
    const Common::Rect& screenRect() const { return myActiveVidMode.screenR; }

    /**
      Returns the dimensions of the mode specific users' desktop, or if
      BufferType::None, return the dimensions of the current active screen.
    */
    const Common::Size& desktopSize(BufferType bufferType = BufferType::None) const {
      return myDesktopSize.at(displayId(bufferType));
    }

    /**
      Get the supported renderers for the video hardware.

      @return  An array of supported renderers
    */
    const VariantList& supportedRenderers() const { return myRenderers; }

    /**
      This method is called to get the specified ARGB data from the viewable
      FrameBuffer area.  Note that this isn't the same as any internal
      surfaces that may be in use; it should return the actual data as it
      is currently seen onscreen.

      Currently this is used only for taking PNG snapshots.  As such, it is slow
      and should not be used for anything else.
    */
    const FBSurface& compositedSurface() {
      return myBackend->compositedSurface();
    }

    /**
      Toggles between fullscreen and window mode.
    */
    void toggleFullscreen(bool toggle = true);

    /**
      Sets the state of the cursor (hidden or grabbed) based on the
      current mode.
    */
    void setCursorState();

    /**
      Enable/disable text events (distinct from single-key events).
    */
    void enableTextEvents(bool enable);

    /**
      Checks if mouse grabbing is allowed.
    */
    bool grabMouseAllowed();

    /**
      Sets the use of grabmouse.
    */
    void enableGrabMouse(bool enable);

    /**
      Toggles the use of grabmouse (only has effect in emulation mode).
    */
    void toggleGrabMouse(bool toggle = true);

    /**
      Query whether grabmouse is enabled.
    */
    bool grabMouseEnabled() const { return myGrabMouse; }

    /**
      Informs the Framebuffer of a change in EventHandler state.
    */
    void stateChanged(EventHandlerState state);

    /**
      Answer whether hidpi mode is allowed.  In this mode, all FBSurfaces
      are scaled to 2x normal size.
    */
    bool hidpiAllowed() const { return myHiDPIAllowed.at(displayId()); }

    /**
      Answer whether hidpi mode is enabled.  In this mode, all FBSurfaces
      are scaled to 2x normal size.
    */
    bool hidpiEnabled() const { return myHiDPIEnabled.at(displayId()); }
    uInt32 hidpiScaleFactor() const { return myHiDPIEnabled.at(displayId()) ? 2 : 1; }

    /**
      This method should be called to save the current settings of all
      its subsystems.  Note that the this may be called when the class
      hasn't been fully initialized, so we first need to check if the
      subsytems actually exist.
    */
    void saveConfig(Settings& settings) const;

  #ifdef GUI_SUPPORT
    /**
      Get the font object(s) of the framebuffer
    */
    const GUI::Font& font() const { return *myFont; }
    const GUI::Font& infoFont() const { return *myInfoFont; }
    const GUI::Font& smallFont() const { return *mySmallFont; }
    const GUI::Font& launcherFont() const { return *myLauncherFont; }

    /**
      Get the font description from the font name

      @param name  The settings name of the font

      @return  The description of the font
    */
    static FontDesc getFontDesc(string_view name);

    /**
      Determine minimal zoom level for a given font - this default impl does nothing

      @param fd Font description
    */
    virtual void setMinZoom(const FontDesc &fd) { }

  #endif  // GUI_SUPPORT

    /**
      Shows or hides the cursor based on the given boolean value.
    */
    void showCursor(bool show) { myBackend->showCursor(show); }

    /**
      Answers if the display is currently in fullscreen mode.
    */
    bool fullScreen() const { return myBackend->fullScreen(); }

    /**
      Updates theme according to OS setting.

      @return  true if theme has changed
    */
    bool updateTheme();

    /**
      Retrieve the R/G/B/A masks from the FrameBuffer backend renderer.
    */
    uInt32 rMask() const { return myBackend->rMask(); }
    uInt32 gMask() const { return myBackend->gMask(); }
    uInt32 bMask() const { return myBackend->bMask(); }
    uInt32 aMask() const { return myBackend->aMask(); }

    /**
      Clear the framebuffer.
    */
    void clear() { myBackend->clear(); }
    void flush() { myBackend->flush(); }

    /**
      Transform from window to renderer coordinates, x/y direction.
     */
    int scaleX(int x) const { return myBackend->scaleX(x); }
    int scaleY(int y) const { return myBackend->scaleY(y); }

    /**
      Get the window ID used for the current mode.
    */
    uInt32 windowId(BufferType bufferType = BufferType::None) const;

    bool isWindowId(uInt32 id) const { return id == windowId(); }

  protected:
    /**
      These methods are used to load/save position and display of the
      current window.
    */
    string getPositionKey() const;
    string getDisplayKey(BufferType bufferType = BufferType::None) const;
    void saveCurrentWindowPosition() const;

    /**
      Frees and reloads all surfaces that the framebuffer knows about.
    */
    void resetSurfaces();

    /**
      Get the display used for the current mode.
    */
    uInt32 displayId(BufferType bufferType = BufferType::None) const;

    /**
      Build an applicable video mode based on the current settings in
      effect, whether TIA mode is active, etc.  Then tell the backend
      to actually use the new mode.

      @return  Whether the operation succeeded or failed
    */
    virtual FBInitStatus applyVideoMode() = 0;

    /**
      Calculate the maximum level by which the base window can be zoomed and
      still fit in the desktop screen.
    */
    virtual double maxWindowZoom() const = 0;

    /**
      Enables/disables fullscreen mode.
    */
    void setFullscreen(bool enable);

  #ifdef GUI_SUPPORT
    /**
      Setup the UI fonts
    */
    void setupFonts();
  #endif  // GUI_SUPPORT

  protected:
    // The parent system for the framebuffer
    OSystem& myOSystem;

    // Backend used for all platform-specific graphics operations
    unique_ptr<FBBackend> myBackend;

    // Indicates the number of times the framebuffer was initialized
    uInt32 myInitializedCount{0};

    // Maximum dimensions of each attached display desktop area
    // Note that this takes 'hidpi' mode into account, so in some cases
    // it will be less than the absolute desktop size
    std::unordered_map<uInt32, Common::Size> myDesktopSize;

    // Maximum absolute dimensions of each attached display desktop area
    std::unordered_map<uInt32, Common::Size> myAbsDesktopSize;

    // The resolution of each attached display in fullscreen mode
    // Windowed modes use myDesktopSize directly
    std::unordered_map<uInt32, Common::Size> myFullscreenDisplays;

    // The resolution of each attached display in windowed mode
    std::unordered_map<uInt32, Common::Size> myWindowedDisplays;

    // HiDPI settings of each attached display
    std::unordered_map<uInt32, bool> myHiDPIAllowed;
    std::unordered_map<uInt32, bool> myHiDPIEnabled;

    // Supported renderers
    VariantList myRenderers;

    // Flag for pending render
    bool myPendingRender{false};

    // The VideoModeHandler class takes responsibility for all video
    // mode functionality
    VideoModeHandler myVidModeHandler;
    VideoModeHandler::Mode myActiveVidMode;

    // Type of the frame buffer
    BufferType myBufferType{BufferType::None};

  #ifdef GUI_SUPPORT
    // The font object to use for the normal in-game GUI
    unique_ptr<GUI::Font> myFont;

    // The info font object to use for the normal in-game GUI
    unique_ptr<GUI::Font> myInfoFont;

    // The font object to use when space is very limited
    unique_ptr<GUI::Font> mySmallFont;

    // The font object to use for the ROM launcher
    unique_ptr<GUI::Font> myLauncherFont;
  #endif  // GUI_SUPPORT

    // The FBMessageHandler class takes responsibility for all onscreen
    // message and frame-statistics overlay functionality
    FBMessageHandler myMsgHandler;

    bool myGrabMouse{false};

    // Holds a reference to all the surfaces that have been created
    std::list<shared_ptr<FBSurface>> mySurfaceList;

    FullPaletteArray myFullPalette{0};
    // Holds UI palette data (for each variation)
    static UIPaletteArray ourStandardUIPalette, ourClassicUIPalette,
                          ourLightUIPalette, ourDarkUIPalette;

  private:
    // Following constructors and assignment operators not supported
    FrameBuffer() = delete;
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer(FrameBuffer&&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    FrameBuffer& operator=(FrameBuffer&&) = delete;
};

#endif  // FRAME_BUFFER_HXX
