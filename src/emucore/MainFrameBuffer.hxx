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

#ifndef MAIN_FRAME_BUFFER_HXX
#define MAIN_FRAME_BUFFER_HXX

#include "FrameBuffer.hxx"

class TIASurface;
class Bezel;

#include "TIAConstants.hxx"

/**
  This class encapsulates all video buffers and is the basis for the video
  display in Stella.  The FBBackend object contained in this class is
  platform-specific, and most rendering tasks are delegated to it.

  The TIA is drawn here, and all GUI elements (ala ScummVM, which are drawn
  into FBSurfaces), are in turn drawn here as well.

  @author  Stephen Anthony
*/
class MainFrameBuffer : public FrameBuffer
{

  public:
    explicit MainFrameBuffer(OSystem& osystem);
    ~MainFrameBuffer();

    /**
      Initialize the framebuffer object (set up the underlying hardware).
      Throws an exception upon encountering any errors.
    */
    void initialize() override;

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
    FBInitStatus createDisplay(string_view title, BufferType type,
                               Common::Size size, bool honourHiDPI = true);

    /**
      Updates the display, which depending on the current mode could mean
      drawing the TIA, any pending menus, etc.
    */
    virtual void update(UpdateMode mode = UpdateMode::NONE);

    /**
      There is a dedicated update method for emulation mode.
    */
    void updateInEmulationMode(float framesPerSecond);

    /**
      Enable/disable any pending messages.  Disabled messages aren't removed
      from the message queue; they're just not redrawn into the framebuffer.
    */
    virtual void enableMessages(bool enable);

    /**
      Set up the TIA/emulation palette.  Due to the way the palette is stored,
      a call to this method implicitly calls setUIPalette() too.

      @param rgb_palette  The array of colors in R/G/B format
    */
    void setTIAPalette(const PaletteArray& rgb_palette);

    /**
      Set disassembly syntax colors.  The active UI theme determines which
      disasm palette is used: light themes (standard, light) use the standard
      disasm palette; dark themes (classic, dark) use the dark one.
      Called automatically by setUIPalette(); can also be called standalone
      when only the disassembly palette needs refreshing.
    */
    void setDisasmPalette();

    /**
      Get the minimum/maximum supported TIA zoom level (windowed mode)
      for the framebuffer.
    */
    double supportedTIAMinZoom() const { return myTIAMinZoom * hidpiScaleFactor(); }
    double supportedTIAMaxZoom() const { return maxWindowZoom(); }

    /**
      Get the TIA surface associated with the framebuffer.
      Note that this is the 'raw' TIA surface, without any post-processing
      effects included.
    */
    TIASurface& tiaSurface() const { return *myTIASurface; }

  #ifdef ADAPTABLE_REFRESH_SUPPORT
    /**
      Toggles between adapt fullscreen refresh rate on and off.
    */
    void toggleAdaptRefresh(bool toggle = true);
  #endif

    /**
      Changes the fullscreen overscan.

      @param direction  +1 indicates increase, -1 indicates decrease
    */
    void changeOverscan(int direction = +1);

    /**
      This method is called when the user wants to switch to the previous/next
      available TIA video mode.  In windowed mode, this typically means going
      to the next/previous zoom level.  In fullscreen mode, this typically
      means switching between normal aspect and fully filling the screen.

      @param direction  +1 indicates next mode, -1 indicates previous mode
    */
    void switchVideoMode(int direction = +1);

    /**
      Toggles the bezel display.
    */
    void toggleBezel(bool toggle = true);

    /**
      Set palette for user interface.
    */
    void setUIPalette();

    /**
      Build an applicable video mode based on the current settings in
      effect, whether TIA mode is active, etc.  Then tell the backend
      to actually use the new mode.

      @return  Whether the operation succeeded or failed
    */
    virtual FBInitStatus applyVideoMode();

    /**
      Calculate the maximum level by which the base window can be zoomed and
      still fit in the desktop screen.
    */
    virtual double maxWindowZoom() const;

    /**
      Determine minimal zoom level for a given font

      @param fd Font description
    */
    virtual void setMinZoom(const FontDesc &fd);

  protected:

    /**
      Renders TIA and overlaying, optional bezel surface

      @param doClear  Clear the framebuffer before rendering
      @param shade    Shade the TIA surface after rendering
    */
    //void renderTIA(bool shade = false, bool doClear = true);
    void renderTIA(bool doClear = true, bool shade = false);

    // The TIASurface class takes responsibility for TIA rendering
    shared_ptr<TIASurface> myTIASurface;

    // The BezelSurface which blends over the TIA surface
    unique_ptr<Bezel> myBezel;

    // Minimum TIA zoom level that can be used for this framebuffer
    double myTIAMinZoom{2.};

    // Holds disassembly palette data (independent of UI theme)
    static DisasmPaletteArray ourStandardDisasmPalette, ourDarkDisasmPalette;

  private:
    // Following constructors and assignment operators not supported
    MainFrameBuffer() = delete;
    MainFrameBuffer(const MainFrameBuffer&) = delete;
    MainFrameBuffer(MainFrameBuffer&&) = delete;
    MainFrameBuffer& operator=(const MainFrameBuffer&) = delete;
    MainFrameBuffer& operator=(MainFrameBuffer&&) = delete;
};

#endif  // FRAME_BUFFER_HXX
