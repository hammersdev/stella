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

#include "Command.hxx"
#include "Dialog.hxx"
#include "FBSurface.hxx"
#include "GuiObject.hxx"
#include "ColorWidget.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ColorWidget::ColorWidget(GuiObject* boss, const GUI::Font& font,
                         int x, int y, int w, int h, int cmd, bool framed)
  : Widget(boss, font, x, y, w, h),
    CommandSender(boss),
    _framed{framed},
    _cmd{cmd}
{
  _flags = Widget::FLAG_ENABLED | Widget::FLAG_CLEARBG;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ColorWidget::setColor(ColorId color)
{
  if(
    (_color != color)
#ifdef MEMVIEW_SUPPORT
    || _rgbMode
#endif
  )
  {
    _color = color;
#ifdef MEMVIEW_SUPPORT
    _rgbMode = false;
#endif
    setDirty();
  }
}

#ifdef MEMVIEW_SUPPORT
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ColorWidget::setColorRgb(uInt32 color)
{
  if((color != _colorRgb) || !_rgbMode)
  {
    _colorRgb = color;
    _rgbMode = true;
    setDirty();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 ColorWidget::getColorRgb() const
{
  if (_rgbMode)
    return _colorRgb;
  else
    return FBSurface::getColorRgb(_color);
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ColorWidget::setCrossed(bool enable)
{
  if(_crossGrid != enable)
  {
    _crossGrid = enable;
    setDirty();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ColorWidget::drawWidget(bool hilite)
{
  FBSurface& s = dialog().surface();

  if(_framed)
  {
    // Draw a thin frame around us.
    s.frameRect(_x, _y, _w, _h + 1, kColor);

    // Show the currently selected color
    if (isEnabled())
    {
#ifdef MEMVIEW_SUPPORT
      if (!_rgbMode)
#endif
        s.fillRect(_x + 1, _y + 1, _w - 2, _h - 1, _color);
#ifdef MEMVIEW_SUPPORT
      else
        s.fillRectRgb(_x + 1, _y + 1, _w - 2, _h - 1, _colorRgb);
#endif
    }
    else
    {
      s.fillRect(_x + 1, _y + 1, _w - 2, _h - 1, kWidColor);
    }
  }
  else
  {
    s.fillRect(_x, _y, _w, _h, isEnabled() ? _color : kWidColor);
  }

  // Cross out the grid?
  if(_crossGrid)
  {
    s.line(_x + 1, _y + 1, _x + _w - 2, _y + _h - 1, kColor);
    s.line(_x + _w - 2, _y + 1, _x + 1, _y + _h - 1, kColor);
  }
}
