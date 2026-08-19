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

#ifndef SCROLL_BAR_H_WIDGET_HXX
#define SCROLL_BAR_H_WIDGET_HXX

class GuiObject;

#include "Widget.hxx"
#include "Command.hxx"
#include "bspf.hxx"

class ScrollBarHWidget : public Widget, public CommandSender
{
  public:
    ScrollBarHWidget(GuiObject* boss, const GUI::Font& font,
                    int x, int y, int w, int h);
    ~ScrollBarHWidget() override = default;

    void recalc();
    void handleMouseDown(int x, int y, MouseButton b, int clickCount) override;
    void handleMouseUp(int x, int y, MouseButton b, int clickCount) override;
    void handleMouseMoved(int x, int y) override;
    void handleMouseWheel(int x, int y, int direction) override;
    bool handleMouseClicks(int x, int y, MouseButton b) override;
    void handleMouseLeft() override;

    static void setWheelLines(int lines) { S_WHEEL_LINES = lines; }
    static int  getWheelLines()          { return S_WHEEL_LINES;  }
    static int scrollBarHeight(const GUI::Font& font) {
      return font.getFontHeight() < 24 ? 15 : 23;
    }

  protected:
    void drawWidget(bool hilite) override;

  private:
    void checkBounds(int old_pos);
    void setArrows();

  public:  // TODO: these shouldn't be public
    int _numEntries{0};
    int _entriesPerPage{0};
    int _currentPos{0};
    int _wheel_lines{0};

  private:
    enum class Part: uInt8 { None, LeftArrow, RightArrow, Slider, PageLeft, PageRight };

    Part _part{Part::None};
    Part _draggingPart{Part::None};
    int _sliderWidth{0};
    int _sliderPos{0};
    int _sliderDeltaMouseDownPos{0};
    int _leftRightWidth{0};
    int _leftRightHeight{0};
    int _leftRightBoxWidth{0};
    int _scrollBarHeight{0};
    const uInt32* _leftImg{nullptr};
    const uInt32* _rightImg{nullptr};

    static inline int S_WHEEL_LINES = 4;

  private:
    // Following constructors and assignment operators not supported
    ScrollBarHWidget() = delete;
    ScrollBarHWidget(const ScrollBarHWidget&) = delete;
    ScrollBarHWidget(ScrollBarHWidget&&) = delete;
    ScrollBarHWidget& operator=(const ScrollBarHWidget&) = delete;
    ScrollBarHWidget& operator=(ScrollBarHWidget&&) = delete;
};

#endif  // SCROLL_BAR_H_WIDGET_HXX
