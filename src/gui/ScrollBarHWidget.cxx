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

#include "Dialog.hxx"
#include "FBSurface.hxx"
#include "ScrollBarHWidget.hxx"
#include "bspf.hxx"

/*
 * TODO:
 * - If there are less items than fit on one pages, no scrolling can be done
 *   and we thus should not highlight the arrows/slider.
 */

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ScrollBarHWidget::ScrollBarHWidget(GuiObject* boss, const GUI::Font& font,
                                 int x, int y, int w, int h)
  : Widget(boss, font, x, y, w, h),
    CommandSender(boss),
    _scrollBarHeight{scrollBarHeight(font)}
{
  _flags = Widget::FLAG_ENABLED | Widget::FLAG_TRACK_MOUSE | Widget::FLAG_CLEARBG;
  _bgcolor = kWidColor;
  _bgcolorhi = kWidColor;

  setArrows();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::setArrows()
{

  // Small left arrow
  static constexpr std::array<uInt32, 7> left_arrow = {
    0b000111,
    0b001110,
    0b011100,
    0b111000,
    0b011100,
    0b001110,
    0b000111
  };
  // Small right arrow
  static constexpr std::array<uInt32, 7> right_arrow = {
    0b111000,
    0b011100,
    0b001110,
    0b000111,
    0b001110,
    0b011100,
    0b111000
  };

  // Large left arrow
  static constexpr std::array<uInt32, 11> left_arrow_large = {
    0b000001111,
    0b000011110,
    0b000111100,
    0b001111000,
    0b011110000,
    0b111100000,
    0b011110000,
    0b001111000,
    0b000111100,
    0b000011110,
    0b000001111,
  };
  // Large right arrow
  static constexpr std::array<uInt32, 11> right_arrow_large = {
    0b111100000,
    0b011110000,
    0b001111000,
    0b000111100,
    0b000011110,
    0b000001111,
    0b000011110,
    0b000111100,
    0b001111000,
    0b011110000,
    0b111100000
  };

  if(_font.getFontHeight() < 24)
  {
    _leftRightWidth = 6;
    _leftRightHeight = 7;
    _leftRightBoxWidth = 18;
    _leftImg = left_arrow.data();
    _rightImg = right_arrow.data();
  }
  else
  {
    _leftRightWidth = 9;
    _leftRightHeight = 11;
    _leftRightBoxWidth = 27;
    _leftImg = left_arrow_large.data();
    _rightImg = right_arrow_large.data();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::handleMouseDown(int x, int y, MouseButton b,
                                      int clickCount)
{
  // Ignore subsequent mouse clicks when the slider is being moved
  if(_draggingPart == Part::Slider)
    return;

  const int old_pos = _currentPos;

  // Do nothing if there are less items than fit on one page
  if(_numEntries <= _entriesPerPage)
    return;

  if(x <= _leftRightBoxWidth)
  {
    // Up arrow
    _currentPos--;
    _draggingPart = Part::LeftArrow;
  }
  else if(x >= _w - _leftRightBoxWidth)
  {
    // Down arrow
    _currentPos++;
    _draggingPart = Part::RightArrow;
  }
  else if(x < _sliderPos)
  {
    _currentPos -= _entriesPerPage - 1;
  }
  else if(x >= _sliderPos + _sliderWidth)
  {
    _currentPos += _entriesPerPage - 1;
  }
  else
  {
    _draggingPart = Part::Slider;
    _sliderDeltaMouseDownPos = x - _sliderPos;
  }

  // Make sure that _currentPos is still inside the bounds
  checkBounds(old_pos);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::handleMouseUp(int x, int y, MouseButton b,
                                    int clickCount)
{
  _draggingPart = Part::None;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::handleMouseWheel(int x, int y, int direction)
{
  const int old_pos = _currentPos;

  if(_numEntries < _entriesPerPage)
    return;

  if(direction < 0)
    _currentPos -= _wheel_lines ? _wheel_lines : S_WHEEL_LINES;
  else
    _currentPos += _wheel_lines ? _wheel_lines : S_WHEEL_LINES;

  // Make sure that _currentPos is still inside the bounds
  checkBounds(old_pos);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::handleMouseMoved(int x, int y)
{
  // Do nothing if there are less items than fit on one page
  if(_numEntries <= _entriesPerPage)
    return;

  if(_draggingPart == Part::Slider)
  {
    _sliderPos = BSPF::clamp(x - _sliderDeltaMouseDownPos,
        _leftRightBoxWidth, _w - _leftRightBoxWidth - _sliderWidth);

    const int old_pos = _currentPos;
    _currentPos = (_sliderPos - _leftRightBoxWidth) * (_numEntries - _entriesPerPage) /
                  (_w - 2 * _leftRightBoxWidth - _sliderWidth);
    checkBounds(old_pos);
  }
  else
  {
    const Part old_part = _part;

    if(x <= _leftRightBoxWidth)   // Up arrow
      _part = Part::LeftArrow;
    else if(x >= _w - _leftRightBoxWidth)	// Down arrow
      _part = Part::RightArrow;
    else if(x < _sliderPos)
      _part = Part::PageLeft;
    else if(x >= _sliderPos + _sliderWidth)
      _part = Part::PageRight;
    else
      _part = Part::Slider;

    if(old_part != _part)
      setDirty();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ScrollBarHWidget::handleMouseClicks(int x, int y, MouseButton b)
{
  // Let continuous mouse clicks come through, as the scroll buttons need them
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::checkBounds(int old_pos)
{
  if(_numEntries <= _entriesPerPage || _currentPos < 0)
    _currentPos = 0;
  else if(_currentPos > _numEntries - _entriesPerPage)
    _currentPos = _numEntries - _entriesPerPage;

  if(old_pos != _currentPos)
  {
    recalc();
    setDirty();
    sendCommand(GuiObject::kSetPositionCmd, _currentPos, _id);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::handleMouseLeft()
{
  _part = Part::None;
  Widget::handleMouseLeft();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::recalc()
{
  const int oldSliderWidth = _sliderWidth,
            oldSliderPos = _sliderPos;

  if(_numEntries > _entriesPerPage)
  {
    _sliderWidth = std::max(_leftRightBoxWidth,
        (_w - 2 * _leftRightBoxWidth) * _entriesPerPage / _numEntries);

    _sliderPos = std::max(0,
      _leftRightBoxWidth + (_w - 2 * _leftRightBoxWidth - _sliderWidth) *
      _currentPos / (_numEntries - _entriesPerPage));
  }
  else
  {
    _sliderWidth = _h - 2 * _leftRightBoxWidth;
    _sliderPos = _leftRightBoxWidth;
  }

  if(oldSliderWidth != _sliderWidth || oldSliderPos != _sliderPos)
    setDirty(); // only set dirty when something changed
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ScrollBarHWidget::drawWidget(bool hilite)
{
  FBSurface& s = _boss->dialog().surface();
  const int rightX = _x + _w;
  const bool isSinglePage = (_numEntries <= _entriesPerPage);

  s.frameRect(_x, _y, _w, _h, hilite ? kWidColorHi : kColor);

  if(_draggingPart != Part::None)
    _part = _draggingPart;

  // Left arrow
  if(hilite && _part == Part::LeftArrow)
    s.fillRect(_x + 1, _y + 1, _leftRightBoxWidth - 2, _h - 2, kScrollColor);

/*
  s.drawBitmap(_upImg, _x + (_scrollBarWidth - _upDownWidth) / 2,
               _y + (_upDownBoxHeight - _upDownHeight) / 2,
               isSinglePage ? kColor
                            : (hilite && _part == Part::UpArrow) ? kWidColor : kTextColor,
               _upDownWidth, _upDownHeight);
*/

  s.drawBitmap(_leftImg, _x + (_leftRightBoxWidth - _leftRightWidth) / 2,
               _y + (_scrollBarHeight - _leftRightHeight) / 2 ,
               isSinglePage ? kColor
                            : (hilite && _part == Part::LeftArrow) ? kWidColor : kTextColor,
               _leftRightWidth, _leftRightHeight);


#if 0

  if(hilite && _part == Part::DownArrow)
    s.fillRect(_x + 1, bottomY - _upDownBoxHeight + 1, _w - 2, _upDownBoxHeight - 2, kScrollColor);
  s.drawBitmap(_downImg, _x + (_scrollBarWidth - _upDownWidth) / 2,
               bottomY - _upDownBoxHeight + (_upDownBoxHeight - _upDownHeight) / 2,
               isSinglePage ? kColor
                            : (hilite && _part == Part::DownArrow) ? kWidColor : kTextColor,
               _upDownWidth, _upDownHeight);

#endif

  // Right arrow
  if(hilite && _part == Part::RightArrow)
    s.fillRect(rightX - _leftRightBoxWidth + 1, _y + 1, _leftRightBoxWidth - 2, _h - 2, kScrollColor);
  s.drawBitmap(_rightImg, rightX - _leftRightBoxWidth + (_leftRightBoxWidth - _leftRightWidth) / 2,
               _y + (_scrollBarHeight - _leftRightHeight) / 2,
               isSinglePage ? kColor
                            : (hilite && _part == Part::RightArrow) ? kWidColor : kTextColor,
               _leftRightWidth, _leftRightHeight);




  // Slider
  if(!isSinglePage)
  {
    // align slider to scroll intervals
    const int alignedPos = std::max(0,
      _leftRightBoxWidth + (_w - 2 * _leftRightBoxWidth - _sliderWidth) *
      _currentPos / (_numEntries - _entriesPerPage));
/*
    s.fillRect(_x + 1, _y + alignedPos - 1, _w - 2, _sliderHeight + 2,
              (hilite && _part == Part::Slider) ? kScrollColorHi : kScrollColor);
*/
    s.fillRect(_x + alignedPos - 1, _y + 1, _sliderWidth + 2, _h - 2, 
              (hilite && _part == Part::Slider) ? kScrollColorHi : kScrollColor);
  }

  clearDirty();
}
