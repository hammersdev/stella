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

#ifndef MEMVIEW_HXX
#define MEMVIEW_HXX

class OSystem;
class MemViewDialog;

#include "DialogContainer.hxx"

/**
  The dialog container for the Memory View Window.

  @author Christian Hammers
*/

class MemView : public DialogContainer
{
  public:

    static constexpr std::string_view MEMVIEW_TITLE = "Stella Memory View";

    /**
      Create a new menu stack
    */
    explicit MemView(OSystem& osystem, FrameBuffer& framebuffer);
    ~MemView() override;

    /**
      Initialize the video subsystem wrt this class.
    */
    FBInitStatus initializeVideo();

    /**
      Return the bottom-most dialog of this container.
    */
    Dialog* baseDialog() override;

    /**
      Set flag if the rendering is forced or not to skip any planned
      rendering optimizations.

      @param forced   Flag selecting if forced or not
    */
    void setForcedUpdate(bool forced);

  private:
    // Pointer holding the base dialog
    MemViewDialog* myBaseDialog{nullptr};

    // The dimensions of this dialog
    Common::Size mySize;

  private:
    // Following constructors and assignment operators not supported
    MemView() = delete;
    MemView(const MemView&) = delete;
    MemView(MemView&&) = delete;
    MemView& operator=(const MemView&) = delete;
    MemView& operator=(MemView&&) = delete;
};

#endif  // MEMVIEW_HXX
