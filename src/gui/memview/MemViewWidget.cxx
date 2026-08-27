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
#include "Debugger.hxx"
#include "DebuggerParser.hxx"
#include "CartDebug.hxx"
#include "FBSurface.hxx"
#include "Dialog.hxx"
#include "ToolTip.hxx"
#include "ScrollBarVWidget.hxx"
#include "ScrollBarHWidget.hxx"
#include "ContextMenu.hxx"
#include "EventHandler.hxx"
#include "CpuDebug.hxx"
#ifdef IMAGE_SUPPORT
#include "PNGLibrary.hxx"
#include "Console.hxx"
#endif
#include "MemViewFrameBuffer.hxx"
#include "MemViewWidget.hxx"
#include "MemViewDialog.hxx"
#include <cmath>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewWidget::MemViewWidget(GuiObject *boss, const GUI::Font& font,
  const int x, const int y, const int w, const int h,
  const uInt16 bankSize, const uInt16 bankCount, const uInt16 bankHeight,
  const bool isZoomable, bool singleRow, const bool separators, 
  const MemViewWidget::ColorTab& readColorTab,
  const MemViewWidget::ColorTab& writeColorTab,
  const MemViewWidget::ColorTab& pcColorTab,
  const uInt32 dataDefaultColor, const uInt32 dataFadedColor,
  const string_view typeText,
  const uInt16 baseAddress, const int mirrorAddrOffset
)
  : Widget(boss, font, x, y, w, h),
    myInnerSurfaceX{x + FRAME_THICKNESS},
    myInnerSurfaceY{y + FRAME_THICKNESS},
    myIsZoomable{isZoomable},
    myParams{bankSize, bankCount, baseAddress, instance().console().cartridge(), myInnerSurfaceX, myInnerSurfaceY},
    myDataLayer{dialog(), myParams, dataDefaultColor, dataFadedColor},
    myReadLayer{dialog(), myParams, readColorTab},
    myWriteLayer{dialog(), myParams, writeColorTab},
    myPcLayer{dialog(), myParams, pcColorTab},
    myPcMarker{dialog(), myParams, MemViewDialog::PC_COLOR_HIGH | 0xFF000000},
    myMouseMarker{dialog(), myParams, FBSurface::getColorRgb(kWidColorHi)},
    myTypeText{typeText},
    myMirrorAddrOffset{mirrorAddrOffset}
{
  _flags = Widget::FLAG_ENABLED |
           Widget::FLAG_RETAIN_FOCUS | Widget::FLAG_TRACK_MOUSE;
  _bgcolor = _bgcolorhi = kDlgColor;

  // Set maximal available inner surface dimensions
  myInnerSurfaceW = w - 2 * FRAME_THICKNESS
    - (isZoomable ? (ScrollBarVWidget::scrollBarWidth(font) - FRAME_THICKNESS) : 0);
  myInnerSurfaceH = h - 2 * FRAME_THICKNESS
    - (isZoomable ? (ScrollBarHWidget::scrollBarHeight(font) - FRAME_THICKNESS) : 0);

  // Check if bank is displayable
  myIsSetup =
    ((bankSize * bankCount) > 0)
    &&
    (
      (
        (bankCount == 1)
        &&
        ((bankSize % bankHeight) == 0)
      )
      ||
      (
        ((bankSize % MemViewDialog::MAX_BANK_HEIGHT) == 0)
        &&
        ((bankSize % bankHeight) == 0)
      )
    );

  Logger::debug(std::format("New MemViewWidget"));

  uInt16 bankWidth = 0;
  if (isSetup())
  {
    bankWidth = bankSize / bankHeight;
    Logger::debug(std::format("Data size   = {}", myParams.myDataSize));
    Logger::debug(std::format("Banks       = {}", bankCount));
    Logger::debug(std::format("Bank size   = {}", bankSize));
    Logger::debug(std::format("Bank width  = {}", bankWidth));
    Logger::debug(std::format("Bank height = {}", bankHeight));
  }
  else
  {
    Logger::error(std::format("MemViewWidget: Unsupported bank size: {}", bankSize));
    // Setup some emergency values to prevent crashing
    bankWidth = DEFAULT_BANK_SIZE / bankHeight;
    myParams.myBankSize = DEFAULT_BANK_SIZE;
    myParams.myBankCount = 1;
    myParams.myDataSize = DEFAULT_BANK_SIZE;
  }

  // Find the initial best layout to use (how are the banks arranged)
  int minZoomLevel = 1;
  int hBanks = 0;
  int vBanks = 0;
  Common::Size size = findBestLayout(bankWidth, bankHeight, separators, singleRow,
    hBanks, vBanks, minZoomLevel);

  // Create scrollbars if the view should be zoomable
  if (isZoomable)
  {
    myVScrollBar = new ScrollBarVWidget(boss, font, myInnerSurfaceX + myInnerSurfaceW, y,
      ScrollBarVWidget::scrollBarWidth(font), h);
    myVScrollBar->setTarget(this);

    myHScrollBar = new ScrollBarHWidget(boss, font, x, 
      myInnerSurfaceY + myInnerSurfaceH, w - ScrollBarVWidget::scrollBarWidth(font),
      ScrollBarHWidget::scrollBarHeight(font));
    myHScrollBar->setTarget(this);
  }
  else
  {
    // Case with no scroll bars = not zoomable -> widget size is only as big as necessary
    myInnerSurfaceW = size.w;
    myInnerSurfaceH = size.h;
    setWidth(myInnerSurfaceW + 2 * FRAME_THICKNESS);
    setHeight(myInnerSurfaceH + 2 * FRAME_THICKNESS);
  }

  // Update parameters with surface size for layers
  myParams.mySurfaceWidth = myInnerSurfaceW;
  myParams.mySurfaceHeight = myInnerSurfaceH;

  if (!isSetup())
  {
    // Show error message
    const int fontHeight = font.getFontHeight();
    new StaticTextWidget(boss, font, myInnerSurfaceX, 
      myInnerSurfaceY + (myInnerSurfaceH - fontHeight) / 2,
      myInnerSurfaceW, fontHeight, TEXT_UNSUPPORTED,
      TextAlign::Center
    );
  }

  // Create context menu for commands
  VariantList l;
  VarList::push_back(l, "Toggle breakpoint", "bp");
  VarList::push_back(l, "Toggle R/W trap", "rwt");
  VarList::push_back(l, "Toggle read trap", "rt");
  VarList::push_back(l, "Toggle write trap", "wt");
  VarList::push_back(l, "Show totals", "st");
#ifdef IMAGE_SUPPORT
  VarList::push_back(l, "Save data picture", "pic");
#endif
  myMenu = new ContextMenu(this, font, l);

  // Resize data array
  myCurrentData.resize(myParams.myDataSize);

  addFocusWidget(this);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewWidget::~MemViewWidget()
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::setDirty(bool renderGui)
{
  if (renderGui)
    setDirty();
  else
    // Don't tell parents about my dirtyness
    // to prevent the GUI redraw everything
    _dirty = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::setDirtyData()
{
  myDataIsDirty = true;
  setDirty(false);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::clearEverythingDirty()
{
  clearDirty();
  myDataIsDirty = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::updateMarker()
{
  myPcMarker.update();
  myMouseMarker.update();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::updateRest()
{
  if (!isSetup())
    return;

  bool debuggerActive = (instance().eventHandler().state() == EventHandlerState::DEBUGGER);

  // Update ToolTip (even if the mouse didn't move)
  if (!debuggerActive)
    dialog().tooltip().refresh(this);

  // Care about PC counter marker
  if (debuggerActive)
  {
    // Check if the PC is in our address range at all
    const uInt16 pc = instance().debugger().cpuDebug().pc();
    const uInt16 pc13 = pc & ((1 << 13) - 1);
    const uInt16 base13 = myParams.myBaseAddress & ((1 << 13) - 1);

    if (
      (pc13 >= base13)
      &&
      (pc13 < (base13 + myParams.myBankSize))
    )
    {
      // PC is within our range
      const int bank = (base13 & 0x1000) ? instance().debugger().cartDebug().getPCBank() : 0;
      const int offset = bank * myParams.myBankSize + pc13 - base13;
      const int byteOffset = myParams.getRearrangedOffset(offset);

      if (myPcMarker.set(true, byteOffset))
        setDirty(true);

      return;
    }
  }

  // PC marker not active
  if (myPcMarker.set(false))
    setDirty(debuggerActive);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleMouseDown(int x, int y, MouseButton b, int clickCount)
{
  if (!isSetup())
    return;

  x -= 2;
  y -= 2;

  myClickX = x;
  myClickY = y;

  // Button 1 is for 'drag'/movement of the image
  // Button 2 is for context menu
  if(b == MouseButton::LEFT)
  {
    // Indicate mouse drag started/in progress
    myMouseDragging = true;
    dialog().tooltip().hide();
  }
  else if(b == MouseButton::RIGHT)
  {
    myRightClickX = myClickX;
    myRightClickY = myClickY;

    // Add menu at current x, y mouse location
    myMenu->show(x + getLeft(), y + getTop(), dialog().surface().dstRect());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleMouseUp(int x, int y, MouseButton b, int clickCount)
{
  myMouseDragging = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::zoom(int level)
{
  if (!myIsZoomable || (myParams.myZoomLevel == level))
    return;

  // Zoom towards or away from mouse position

  // Limit to positions within the shown data (if mouse is over border)
  int x = myParams.limitPosX(myClickX);
  int y = myParams.limitPosY(myClickY);

  // Bring to current pos within data
  x += myParams.myOffsetX;
  y += myParams.myOffsetY;

  // Multiply with change in zoom level
  const double zoomFactor = static_cast<double>(level) / static_cast<double>(myParams.myZoomLevel);
  x = static_cast<int>(round(static_cast<double>(x) * zoomFactor));
  y = static_cast<int>(round(static_cast<double>(y) * zoomFactor));

  // Apply new zoom
  myParams.myZoomLevel = level;

  // Calculate new correct sizes
  myParams.layoutPreCalc();

  // Set new offsets
  myParams.myOffsetX = myParams.myLeftBorderWidth ? 0 : (x - myClickX);
  myParams.myOffsetY = myParams.myTopBorderHeight ? 0 : (y - myClickY);

  // Calc the rest
  myParams.layoutPostCalc();
  recalcScrollBars();
  updateMarker();
  setDirtyData();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleMouseWheel(int x, int y, int direction)
{
  if (!isSetup())
    return;

  dialog().tooltip().hide();

  x -= 2;
  y -= 2;

  // Zoom towards mouse position
  myClickX = x;
  myClickY = y;

  if(direction > 0)
  {
    // Zoom out
    if(myParams.myZoomLevel > myParams.myMinZoom)
      zoom(myParams.myZoomLevel - 1);
  }
  else
  {
    // Zoom in
    if(myParams.myZoomLevel < myParams.myMaxZoom)
      zoom(myParams.myZoomLevel + 1);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleMouseMoved(int x, int y)
{
  if (!isSetup())
    return;

  x -= 2;
  y -= 2;

  if(myMouseDragging)
  {
    const int diffx = x - myClickX;
    const int diffy = y - myClickY;

    myClickX = x;
    myClickY = y;

    myParams.myOffsetX -= diffx;
    myParams.myOffsetY -= diffy;

    myParams.layoutRecalc();
    recalcScrollBars();
    updateMarker();
    setDirtyData();
  }
  else if (intPosInData(x, y))
  {
    // Data marker
    unsigned int byteOffset = 0;
    int xFracPixels;
    int yFracPixels;
    myParams.getPosition(x - myParams.myLeftBorderWidth, y - myParams.myTopBorderHeight,
      &byteOffset, &xFracPixels, &yFracPixels);
    myMouseMarker.set(true, byteOffset);
    setDirty(false);
  }
  else
  {
    myMouseMarker.set(false);
    setDirty(false);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleMouseLeft()
{
  if (!isSetup())
    return;
  myMouseDragging = false;
  myMouseMarker.set(false);
  setDirty(false);
  Widget::handleMouseLeft();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string MemViewWidget::getToolTip(const Common::Point& pos) const
{
  if (!extPosInData(pos))
    return "";

  const Common::Point internalPos = extPosConv(pos);
  int bank = 0;
  unsigned int offset = 0;
  uInt16 address = myParams.getAddress(internalPos.x, internalPos.y, &bank, &offset);
  uInt16 mirrorAddress = address + myMirrorAddrOffset;

  // Build tip

  // Hexadecimal
  const uInt8 value = myCurrentData[offset];
  string text = std::format("${:0>2X}", value);

  // Decimal
  text += std::format("  {:>4}", std::format("#{}", value));
  if (value >= 0x80)
    text += std::format("/{:<4}", value - 256);
  else
    text += "     ";

  // Binary
  text += std::format("  %{:0>8B}", myCurrentData[offset]);

  // Address
  if ((myParams.myBaseAddress + myParams.myDataSize) <= 0x100)
  {
    // 2-digit address
    text += std::format("\n${:0>2X}", address);
#if 0
    if (mirrorAddress != address)
      text += std::format("/${:0>2X}", mirrorAddress);
#endif
    text += " [" + myTypeText + "]";
  }
  else
  {
    // 4-digit address
    text += std::format("\n${:0>4X}", address);
    if (mirrorAddress != address)
      text += std::format("/${:0>4X}", mirrorAddress);
    text += " [" + myTypeText;
    // Bank?
    if (myParams.myBankCount > 1)
      text += std::format(" Bank {}]", bank);
    else
      text += "]";
  }

  // Total access counters
  text += std::format("\nTotal: PC {}, R {}, W {}",
    myPcLayer.getTotalValue(offset),
    myReadLayer.getTotalValue(offset),
    myWriteLayer.getTotalValue(offset)
  );

  // Delta access counters since last clear
  text += std::format("\nDelta: PC {}, R {}, W {}",
    myPcLayer.getDeltaValue(offset),
    myReadLayer.getDeltaValue(offset),
    myWriteLayer.getDeltaValue(offset)
  );

  // Label (if any)
  CartDebug& cartDebug = instance().debugger().cartDebug();
  string label = cartDebug.getLabel(address, true);
  if (!label.empty())
    text += "\n" + label;

  return text;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::updateData(const ByteSpan& data)
{
  if (!isSetup())
    return;

  if (data.size() != myParams.myDataSize)
  {
    cerr << "MemView data size mismatch (" << data.size() << " != " << myParams.myDataSize << ")\n";
    return;
  }

  myCurrentData.assign(data.begin(), data.end());
  myDataLayer.updateData(myCurrentData);
  setDirtyData();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MemViewWidget::setAccessDataParams(uInt32 size, uInt32 offset)
{
  Logger::debug(std::format("[{}] Access counters offset = {}", myParams.myDataSize, offset));
  Logger::debug(std::format("[{}] Access counters size   = {}", myParams.myDataSize, size));
  return myParams.setAccessDataParams(size, offset);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::updateAccessData(
  Device::AccessCounter* readAccessData,
  Device::AccessCounter* writeAccessData,
  Device::AccessCounter* pcAccessData,
  const uInt32 elapsedCycles,
  const int elapsedFrames
)
{
  if (!isSetup())
    return;

  // Scale heatmap decrement value according to elapsed cycles for all layers
  // Note: this is currently based on NTSC cycles, PAL will be nearly the same
  // This is scaled so that myAccessDataDecrement == 255.0 will let a fully heated up cell
  // of the heatmap fade away within one frame.
  const MemViewAccessLayer::HeatmapValue currentDecrement = static_cast<MemViewAccessLayer::HeatmapValue>(
    (static_cast<double>(elapsedCycles) / 19912.0) * myParams.myAccessDataDecrement
  );

  // Compare new data with last one and update our heatmaps accordingly
  myReadLayer.updateAccessData(readAccessData, currentDecrement, elapsedFrames);
  myWriteLayer.updateAccessData(writeAccessData, currentDecrement, elapsedFrames);
  myPcLayer.updateAccessData(pcAccessData, currentDecrement, elapsedFrames);

  // Update display fields
  heatmapsToFields();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::setLayoutParameters(bool singleRow, bool separators, int bankHeight)
{
  if (!isSetup())
    return;

  const int bankWidth = myParams.myBankSize / bankHeight;
  int hBanks = 0;
  int vBanks = 0;
  int minZoomLevel = 1;

  findBestLayout(bankWidth, bankHeight, separators, singleRow, hBanks, vBanks, minZoomLevel);

  myParams.setLayoutParameters(bankWidth, bankHeight, hBanks, vBanks, minZoomLevel, separators);

  // Set original data again to let the corresponding layer copy the data to it's new layout
  myDataLayer.updateData(myCurrentData);

  // Update heatmap layout to new arrangement when paused or in debugger
  if (instance().eventHandler().state() != EventHandlerState::EMULATION)
    heatmapsToFields(true);

  recalcScrollBars();
  updateMarker();
  setDirtyData();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::setVisualParameters(bool showData, bool showPc, bool showReads, bool showWrites,
      bool inverted, bool byteFade)
{
  if (!isSetup())
    return;

  // Switch on/off visibility of each layer
  myDataLayer.setVisibility(showData);
  myPcLayer.setVisibility(showPc);
  myReadLayer.setVisibility(showReads);
  myWriteLayer.setVisibility(showWrites);

  // Set parameters specific to the data layer
  myDataLayer.setVisualParameters(
    inverted,
    // No byte fade for single column data (e.g. RAM)
    (myParams.myDataSize <= myParams.myBankHeight) ? false : byteFade
  );

  setDirtyData();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::setDecayRate(int percantage)
{
  if (!isSetup())
    return;

  myParams.setDecayRate(percantage);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::clearHeatmaps()
{
  if (!isSetup())
    return;

  myReadLayer.clearHeatmap();
  myWriteLayer.clearHeatmap();
  myPcLayer.clearHeatmap();
  heatmapsToFields(true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MemViewWidget::lockedSingleRow()
{
  // Odd bank counts can only be displayed in single row mode
  return myParams.myBankCount & 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::render()
{
  if (!isSetup())
    return;

  // Render layers bottom to top
  myDataLayer.render();
  myReadLayer.render();
  myWriteLayer.render();
  myPcLayer.render();
  myPcMarker.render();
  myMouseMarker.render();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::drawWidget(bool hilite)
{
  // Draw outer frame
  FBSurface& s = _boss->dialog().surface();
  s.frameRect(_x, _y, _w, _h, hilite ? kWidColorHi : kColor);
  if (!isSetup())
    return;

  // Let the layers draw again if necessary
  if (myDataIsDirty) {
    myDataLayer.draw();
  }
  if (isDirty())
  {
    myReadLayer.draw();
    myWriteLayer.draw();
    myPcLayer.draw();
  }

  clearEverythingDirty();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Common::Point MemViewWidget::extPosConv(const Common::Point& pos) const
{
  return Common::Point(
    BSPF::clamp(pos.x - myInnerSurfaceX - 1, 0, myInnerSurfaceW - 1),
    BSPF::clamp(pos.y - myInnerSurfaceY - 1, 0, myInnerSurfaceH - 1)
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MemViewWidget::extPosInData(const Common::Point& pos) const
{
  return
    (pos.x >= (myInnerSurfaceX + myParams.myLeftBorderWidth + 1))
    &&
    (pos.x < (myInnerSurfaceX + myParams.myLeftBorderWidth + myParams.myCurrentWidth + 1))
    &&
    (pos.y >= (myInnerSurfaceY + myParams.myTopBorderHeight + 1))
    &&
    (pos.y < (myInnerSurfaceY + myParams.myTopBorderHeight + myParams.myCurrentHeight + 1));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MemViewWidget::intPosInData(int x, int y) const
{
  return
    (x >= myParams.myLeftBorderWidth)
    &&
    (x < (myParams.myLeftBorderWidth + myParams.myCurrentWidth))
    &&
    (y >= myParams.myTopBorderHeight)
    &&
    (y < (myParams.myTopBorderHeight + myParams.myCurrentHeight));
}

#ifdef IMAGE_SUPPORT
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::savePicture()
{
  // Build an unique filename
  string ppath = std::format("{}{}", instance().snapshotSaveDir().getPath(),
    instance().console().properties().get(PropType::Cart_Name));
  // Determine if the file already exists, checking each successive filename
  // until one doesn't exist
  if(FSNode(ppath + ".png").exists())
  {
    for(const uInt32 i: std::views::iota(1U))
    {
      const string candidate = std::format("{}_{}.png", ppath, i);
      if(!FSNode(candidate).exists())
      {
        ppath += std::format("_{}", i);
        break;
      }
    }
  }
  ppath += ".png";

  // First build a dummy Params struct and reset some values
  // to get rid of the borders and be able to save the whole thing
  // with a defined zoom level
  MemViewParams dummyParams = myParams;
  if (!dummyParams.mySeparators)
    dummyParams.myZoomLevel = 1;
  dummyParams.layoutPreCalc();
  dummyParams.mySurfaceWidth = dummyParams.myTotalWidth;
  dummyParams.mySurfaceHeight = dummyParams.myTotalHeight;
  dummyParams.layoutPreCalc();
  dummyParams.layoutPostCalc();
  // Let the data layer draw the content
  std::vector<uInt32> dummyData(dummyParams.myTotalWidth * dummyParams.myTotalHeight);
  myDataLayer.drawDirect(dummyParams, dummyData.data());

  // Try to save
  string message = "Picture saved";
  try
  {
    const Common::Rect rect(dummyParams.mySurfaceWidth, dummyParams.mySurfaceHeight);
    PNGLibrary::saveImage(ppath, rect, dummyData.data(), dummyParams.myTotalWidth);
  }
  catch(const std::runtime_error& e)
  {
    message = e.what();
  }
  instance().memViewFrameBuffer().showTextMessage(message);
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::handleCommand(CommandSender* sender, int cmd, int data, int id)
{
  switch (cmd)
  {
    case GuiObject::kSetPositionCmd:
      if ((sender == myVScrollBar) && (myParams.myOffsetY != data))
      {
        myParams.myOffsetY = data;
        myParams.layoutRecalc();
        updateMarker();
        setDirtyData();
      }
      else if ((sender == myHScrollBar) && (myParams.myOffsetX != data))
      {
        myParams.myOffsetX = data;
        myParams.layoutRecalc();
        updateMarker();
        setDirtyData();
      }
      break;
    case ContextMenu::kItemSelectedCmd:
    {
      const string& rmb = myMenu->getSelectedTag().toString();
      if ((rmb == "bp") || (rmb == "rwt") || (rmb == "rt") || (rmb == "wt")) 
      {
        // User wishes to run a debugger function
        int bank = 0;
        uInt16 address = myParams.getAddress(myRightClickX, myRightClickY, &bank);

        Debugger& debugger = instance().debugger();
        bool wasLocked = debugger.systemIsLocked();
        if (!wasLocked)
          debugger.lockSystem();

        // Build debugger command
        string command;
        if (rmb == "bp")
          command = std::format("break ${:X} {}", address, bank);
        else if (rmb == "rwt")
          command = std::format("trap ${:X}", address);
        else if (rmb == "rt")
          command = std::format("trapRead ${:X}", address);
        else if (rmb == "wt")
          command = std::format("trapWrite ${:X}", address);
        else
          assert(false);

        // Run
        const string message = debugger.parser().run(command);
        if (!wasLocked)
          debugger.unlockSystem();
        instance().memViewFrameBuffer().showTextMessage(message);
      }
      else if (rmb == "st")
      {
        // Show totals
        bool goToPause = (instance().eventHandler().state() == EventHandlerState::EMULATION);
        myReadLayer.loadTotals(goToPause);
        myWriteLayer.loadTotals(goToPause);
        myPcLayer.loadTotals(goToPause);
        heatmapsToFields(true);
        if (goToPause)
        {
          // Set current state to paused so the user can actually see the
          // total values before they disappear
          instance().eventHandler().setState(EventHandlerState::PAUSE);
        }
      }
#ifdef IMAGE_SUPPORT
      else if (rmb == "pic")
      {
        // Save a picture of the data
        savePicture();
      }
#endif
      break;
    }
    default:
      break;

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Common::Size MemViewWidget::calcSizeAndZoom(int availableWidth, int availableHeight,
  int bankWidth, int bankHeight, int hBanks, int vBanks,
  bool separators, int& minZoomLevel
)
{
  const int totalSeparatorWidth = (separators && (hBanks >= 2)) ? (hBanks - 1) * MemViewParams::SEPARATOR_WIDTH : 0;
  const int totalSeparatorHeight = (separators && (vBanks >= 2)) ? (vBanks - 1) * MemViewParams::SEPARATOR_HEIGHT : 0;
  const int minHeight = bankHeight * vBanks;
  const int minWidth = bankWidth * 8 * hBanks;

  // Automatically determine minimum zoom level
  const int hZoom = (availableWidth - totalSeparatorWidth) / minWidth;
  const int vZoom = (availableHeight - totalSeparatorHeight) / minHeight;
  minZoomLevel = std::max(std::min(hZoom, vZoom), 1);

  return Common::Size(
    // Width
    minWidth * minZoomLevel + totalSeparatorWidth,
    // Height
    minHeight * minZoomLevel + totalSeparatorHeight
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Common::Size MemViewWidget::findBestLayout(
  const int& bankWidth, const int& bankHeight, const bool& separators, bool& singleRow,
  int& hBanks, int& vBanks, int& minZoomLevel)
{
  if (lockedSingleRow())
    singleRow = true;

  bool fits = false;
  Common::Size size;
  hBanks = myParams.myBankCount;
  vBanks = 1;
  minZoomLevel = 1;

  int w = myInnerSurfaceW;
  int h = myInnerSurfaceH;

  // Determine best format to display the data based on the available area size
  if (singleRow)
  {
    // The easy case - only one bank row
    size = MemViewWidget::calcSizeAndZoom(
      w,  // availableWidth
      h,  // availableHeight
      bankWidth, // bankWidth
      bankHeight, // bankHeight
      hBanks,  // hBanks
      vBanks,  // vBanks
      separators, // separators
      minZoomLevel // minZoomLevel
    );

    fits = (size.w <= static_cast<uInt32>(w)) && (size.h <= static_cast<uInt32>(h));
  }
  else
  {
    const double destAspect = static_cast<double>(w) / static_cast<double>(h);
    int testZoomLevel = 1;

    size = MemViewWidget::calcSizeAndZoom(
      w,  // availableWidth
      h,  // availableHeight
      bankWidth, // bankWidth
      bankHeight, // bankHeight
      hBanks,  // hBanks
      vBanks,  // vBanks
      separators, // separators
      minZoomLevel // minZoomLevel
    );

    fits = (size.w <= static_cast<uInt32>(w)) && (size.h <= static_cast<uInt32>(h));

    double aspectDiff = fabs(
      (static_cast<double>(size.w) / static_cast<double>(size.h))
      -
      destAspect
    );

    int testVBanks = vBanks;
    int testHBanks = hBanks;

    do
    {
      testVBanks *= 2;
      testHBanks /= 2;

      if (!testHBanks)
        break;

      Common::Size testSize = MemViewWidget::calcSizeAndZoom(
        w,  // availableWidth
        h,  // availableHeight
        bankWidth, // bankWidth
        bankHeight, // bankHeight
        testHBanks,  // hBanks
        testVBanks,  // vBanks
        separators, // separators
        testZoomLevel // minZoomLevel
      );

      bool testFits = (testSize.w <= static_cast<uInt32>(w)) && (testSize.h <= static_cast<uInt32>(h));
      double testDiff = fabs(
        (static_cast<double>(testSize.w) / static_cast<double>(testSize.h))
        -
        destAspect
      );

      if (
        (!fits || testFits)
        &&
        (
          (!fits && testFits)
          ||
          (testZoomLevel > minZoomLevel)
          ||
          (testDiff < aspectDiff))
        )
      {
        // Take new best option
        size = testSize; 
        vBanks = testVBanks;
        hBanks = testHBanks;
        minZoomLevel = testZoomLevel;
        aspectDiff = testDiff;
        fits = testFits;
      }

    } while (testHBanks != 1);

  }

  // At least one condition must be met
  assert(fits || myIsZoomable);

  return size;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::heatmapsToFields(bool force)
{
  if (
    !force
    &&
    (!myReadLayer.myVisibility && !myWriteLayer.myVisibility && !myPcLayer.myVisibility)
  )
    return;

  // Precalc some often used values
  const int myBankHeightMinusOne = myParams.myBankHeight - 1;
  const int lineStopAdd = myParams.myBankRowSize - myParams.myBankHeight + 1;
  const int nextLineSub = myParams.myBankRowSize - 1;

  // The raw access data arrays are our sources
  MemViewAccessLayer::HeatmapValue* srcReadData = myReadLayer.myHeatmap.data();
  MemViewAccessLayer::HeatmapValue* srcWriteData = myWriteLayer.myHeatmap.data();
  MemViewAccessLayer::HeatmapValue* srcPcData = myPcLayer.myHeatmap.data();

  // The layers are our destinations
  uInt32* destReadLayer = myReadLayer.myFields.data();
  uInt32* destWriteLayer = myWriteLayer.myFields.data();
  uInt32* destPcLayer = myPcLayer.myFields.data();

  // Stop pointer values for comparison
  // (we only work on the srcReadData here because all three buffers
  // are the same size)
  const MemViewAccessLayer::HeatmapValue* lineStop = srcReadData + myParams.myBankRowSize;
  const MemViewAccessLayer::HeatmapValue* bankRowStop = srcReadData + myParams.myBankRowSize + myBankHeightMinusOne;
  const MemViewAccessLayer::HeatmapValue* fullStop = srcReadData + myParams.myDataSize + myBankHeightMinusOne;

  const bool doRead = force || myReadLayer.myVisibility;
  const bool doWrite = force || myWriteLayer.myVisibility;
  const bool doPc = force || myPcLayer.myVisibility;

  // One total line at the time
  while (1)
  {
    do
    {
      // Read-access data
      if (doRead)
        *(destReadLayer++) = myReadLayer.myColorTab[static_cast<unsigned int>(*srcReadData)];

      // Write-access data
      if (doWrite)
        *(destWriteLayer++) = myWriteLayer.myColorTab[static_cast<unsigned int>(*srcWriteData)];

      // PC-access data
      if (doPc)
        *(destPcLayer++) = myPcLayer.myColorTab[static_cast<unsigned int>(*srcPcData)];

      // Advance src pointers
      srcReadData += myParams.myBankHeight;
      srcWriteData += myParams.myBankHeight;
      srcPcData += myParams.myBankHeight;

    } while (srcReadData != lineStop);

    if (srcReadData == fullStop)
    {
      // Done
      break;
    }
    if (srcReadData == bankRowStop)
    {
      // Next bank row
      srcReadData -= myBankHeightMinusOne;
      srcWriteData -= myBankHeightMinusOne;
      srcPcData -= myBankHeightMinusOne;
      lineStop += lineStopAdd;
      bankRowStop += myParams.myBankRowSize;
    }
    else
    {
      // Next line
      srcReadData -= nextLineSub;
      srcWriteData -= nextLineSub;
      srcPcData -= nextLineSub;
      lineStop++;
    }
  }

  setDirty(false);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewWidget::recalcScrollBars()
{
  if (!myIsZoomable)
    return;

  myVScrollBar->_numEntries = myParams.myTotalHeight;
  myVScrollBar->_entriesPerPage = myParams.myCurrentHeight;
  myVScrollBar->_currentPos = myParams.myOffsetY;
  myVScrollBar->recalc();

  myHScrollBar->_numEntries = myParams.myTotalWidth;
  myHScrollBar->_entriesPerPage = myParams.myCurrentWidth;
  myHScrollBar->_currentPos = myParams.myOffsetX;
  myHScrollBar->recalc();
}
