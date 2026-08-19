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

#ifndef MEMVIEW_FRAME_BUFFER_HXX
#define MEMVIEW_FRAME_BUFFER_HXX

#include "FrameBuffer.hxx"

/**
  This is a derivation of the FrameBuffer class specifically to serve the
  Memory View window.

  @author  Stephen Anthony, Christian Hammers
*/
class MemViewFrameBuffer : public FrameBuffer
{

  public:
    explicit MemViewFrameBuffer(OSystem& osystem);
    ~MemViewFrameBuffer();

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
      Calculate the maximum level by which the base window can be zoomed and
      still fit in the desktop screen. Currently unused.
    */
    virtual double maxWindowZoom() const { return 1.0; }

  protected:

    /**
      Build an applicable video mode based on the current settings in
      effect, whether TIA mode is active, etc.  Then tell the backend
      to actually use the new mode.

      @return  Whether the operation succeeded or failed
    */
    virtual FBInitStatus applyVideoMode();

  private:
    // Following constructors and assignment operators not supported
    MemViewFrameBuffer() = delete;
    MemViewFrameBuffer(const MemViewFrameBuffer&) = delete;
    MemViewFrameBuffer(MemViewFrameBuffer&&) = delete;
    MemViewFrameBuffer& operator=(const MemViewFrameBuffer&) = delete;
    MemViewFrameBuffer& operator=(MemViewFrameBuffer&&) = delete;
};

#endif  // MEMVIEW_FRAME_BUFFER_HXX
