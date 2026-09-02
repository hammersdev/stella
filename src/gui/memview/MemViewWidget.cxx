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
  const uInt16 bankSize, const uInt16 bankCount, uInt16 initialBankHeight,
  const bool isZoomable,
  const MemViewWidget::ColorTab& readColorTab,
  const MemViewWidget::ColorTab& writeColorTab,
  const MemViewWidget::ColorTab& pcColorTab,
  const uInt32 dataDefaultColor, const uInt32 dataFadedColor,
  const string_view typeText,
  const uInt32 baseAddress
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
    myTypeText{typeText}
{
  _flags = Widget::FLAG_ENABLED |
           Widget::FLAG_RETAIN_FOCUS | Widget::FLAG_TRACK_MOUSE;
  _bgcolor = _bgcolorhi = kDlgColor;

  auto [size, layoutParams] = calcNeededSize(font, w, h, isZoomable, bankSize, bankCount, initialBankHeight,
    myIsSetup, &myParams.mySurfaceWidth, &myParams.mySurfaceHeight, &myParams);

  // Create scrollbars if the view should be zoomable
  if (isZoomable)
  {
    myVScrollBar = new ScrollBarVWidget(boss, font, myInnerSurfaceX + myParams.mySurfaceWidth, y,
      ScrollBarVWidget::scrollBarWidth(font), h);
    myVScrollBar->setTarget(this);

    myHScrollBar = new ScrollBarHWidget(boss, font, x, 
      myInnerSurfaceY + myParams.mySurfaceHeight, w - ScrollBarVWidget::scrollBarWidth(font),
      ScrollBarHWidget::scrollBarHeight(font));
    myHScrollBar->setTarget(this);
  }
  else
  {
    // Case with no scroll bars = not zoomable -> widget size is only as big as necessary
    setWidth(size.w);
    setHeight(size.h);
  }

  if (isSetup())
  {
    myParams.setLayoutParameters(layoutParams, false, true);
  }
  else
  {
    // Show error message
    const int fontHeight = font.getFontHeight();
    new StaticTextWidget(boss, font, myInnerSurfaceX, 
      myInnerSurfaceY + (myParams.mySurfaceHeight - fontHeight) / 2,
      myParams.mySurfaceWidth, fontHeight, TEXT_UNSUPPORTED,
      TextAlign::Center
    );
  }

  // Create context menu for commands
  myMenu = new ContextMenu(this, font, getContextMenuItems());

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

  bool stopped = (instance().eventHandler().state() != EventHandlerState::EMULATION);

  // Update ToolTip (even if the mouse didn't move)
  if (!stopped)
    dialog().tooltip().refresh(this);

  // Care about PC counter marker
  if (stopped)
  {
    // Get current bank from the cartridge debugger
    int bank = instance().debugger().cartDebug().getPCBank();

    // Adjust for RAM banks if necessary
    if (myParams.myBaseAddress & QUERY_RAM_BANK_ORIGIN)
      bank -= myParams.myCartridge.ramBankOffset();

    if ((bank >= 0) && (bank < myParams.myBankCount))
    {
      // Check if the PC really is in the address range of that bank
      const uInt16 pc = instance().debugger().cpuDebug().pc();

      Common::RwAddress address = myParams.getBankOrigin(bank, pc);
      if (
        address.valid
        &&
        (pc >= address.read)
        &&
        (pc < (address.read + myParams.myBankSize))
      )
      {
        const int offset = bank * myParams.myBankSize + pc - address.read;
        const int byteOffset = myParams.getRearrangedOffset(offset);

        if (myPcMarker.set(true, byteOffset))
          setDirty(true);

        return;
      }
    }
  }

  // PC marker not active
  if (myPcMarker.set(false))
    setDirty(stopped);
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
  Common::RwAddress address = myParams.getAddress(internalPos.x, internalPos.y, &bank, &offset);

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
  if (
    address.valid
    &&
    !(myParams.myBaseAddress & (QUERY_ROM_BANK_ORIGIN | QUERY_RAM_BANK_ORIGIN)) 
    &&
    ((myParams.myBaseAddress + myParams.myDataSize) <= 0x100)
  )
  {
    // 2-digit address (internal RAM)
    text += std::format("\n${:0>2X}", address.read);
#if 0
    if (address.write != address.read)
      text += std::format("/${:0>2X}", address.write);
#endif
    text += " [" + myTypeText + "]";
  }
  else
  {
    // 4-digit address
    if (address.valid)
    {
      // Normal address display
      text += std::format("\n${:0>4X}", address.read);
      if (address.write != address.read)
        text += std::format("/${:0>4X}", address.write);
    }
    else
    {
      // Invalid address (probably currently not mapped)
      text += std::format("\n(${:0>4X}", address.read);
      if (address.write != address.read)
        text += std::format("/${:0>4X})", address.write);
      else
        text += ")";
    }
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
  if (address.valid)
  {
    CartDebug& cartDebug = instance().debugger().cartDebug();
    string label = cartDebug.getLabel(address.read, true);
    if (!label.empty())
      text += "\n" + label;
  }

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
void MemViewWidget::setLayoutParameters(int bankHeight, bool singleRow, bool separators)
{
  if (!isSetup())
    return;

  // Calculate new arrangement and set to params
  auto [size, layoutParams] = findBestLayout(
    myParams.myBankSize, myParams.myBankCount, myParams.mySurfaceWidth, myParams.mySurfaceHeight,
    myIsZoomable, myIsZoomable ? bankHeight : myParams.myBankHeight, singleRow, separators
  );
  myParams.setLayoutParameters(layoutParams, singleRow, separators);

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
  return (myParams.myVBanks == 1);
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
    BSPF::clamp(pos.x - myInnerSurfaceX - 1, 0, myParams.mySurfaceWidth - 1),
    BSPF::clamp(pos.y - myInnerSurfaceY - 1, 0, myParams.mySurfaceHeight - 1)
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
VariantList MemViewWidget::getContextMenuItems() const
{
  VariantList list;
  VarList::push_back(list, "Toggle breakpoint", "bp");
  VarList::push_back(list, "Toggle R/W trap", "rwt");
  VarList::push_back(list, "Toggle read trap", "rt");
  VarList::push_back(list, "Toggle write trap", "wt");
  VarList::push_back(list, "Show totals", "st");
#ifdef IMAGE_SUPPORT
  VarList::push_back(list, "Save data picture", "pic");
#endif
  if (myIsZoomable)
  {
    VarList::push_back(list, std::format("Bank height 64{}",
      ((myParams.myBankHeight == 64) ? " *" : "")), "bh64");
    VarList::push_back(list, std::format("Bank height 128{}",
      ((myParams.myBankHeight == 128) ? " *" : "")), "bh128");
    VarList::push_back(list, std::format("Bank height 256{}",
      ((myParams.myBankHeight == 256) ? " *" : "")), "bh256");
    VarList::push_back(list, std::format("Bank height 512{}",
      ((myParams.myBankHeight == 512) ? " *" : "")), "bh512");
  }

  return list;
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
        string message;
        int bank = 0;
        Common::RwAddress address = myParams.getAddress(myRightClickX, myRightClickY, &bank);
        if (address.valid)
        {
          Debugger& debugger = instance().debugger();
          bool wasLocked = debugger.systemIsLocked();
          if (!wasLocked)
            debugger.lockSystem();

          // Build debugger command
          string command;
          if (rmb == "bp")
          {
            if (!(myParams.myBaseAddress & QUERY_RAM_BANK_ORIGIN))
              command = std::format("break ${:X} {}", address.read, bank);
            else
              command = std::format("break ${:X}", address.read);
          }
          else if (rmb == "rwt")
          {
            command = std::format("trap ${:X}", address.read);
            if (address.write != address.read)
            {
              debugger.parser().run(command);
              command = std::format("trap ${:X}", address.write);
            }
          }
          else if (rmb == "rt")
            command = std::format("trapRead ${:X}", address.read);
          else if (rmb == "wt")
            command = std::format("trapWrite ${:X}", address.write);
          else
            assert(false);

          // Run
          message = debugger.parser().run(command);
          if (!wasLocked)
            debugger.unlockSystem();
        }
        else
        {
          message = "Address currently not mapped";
        }
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
      else if ((rmb == "bh64") || (rmb == "bh128") || (rmb == "bh256") || (rmb == "bh512"))
      {
        uInt16 bankHeight = static_cast<uInt16>(BSPF::stoi(rmb.substr(2)));
        if (myParams.myBankHeight != bankHeight)
        {
          // Change the bank height
          setLayoutParameters(bankHeight, myParams.mySingleRow, myParams.mySeparators);
          myMenu->addItems(getContextMenuItems());
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
Common::Size MemViewWidget::calcSizeAndZoom(const int availableWidth, const int availableHeight,
  const int bankWidth, const int bankHeight, const int hBanks, const int vBanks,
  const bool separators, int& minZoomLevel
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
std::tuple<Common::Size, MemViewParams::LayoutParams> MemViewWidget::findBestLayout(
  const uInt16& bankSize, const uInt16& bankCount,
  const int& innerSurfaceW, const int& innerSurfaceH,
  const bool& isZoomable,
  const uInt16 bankHeight,
  bool singleRow, const bool separators
)
{
  if (bankCount == 1)
    singleRow = true;

  const double destAspect = static_cast<double>(innerSurfaceW) / static_cast<double>(innerSurfaceH);
  const int firstVBanks = 1;
  const int lastVBanks = singleRow ? 1 : bankCount;
  double bestAspectDiff = -1.0;
  bool fits = false;
  Common::Size bestSize;
  MemViewParams::LayoutParams layoutParams;

  // Determine best format to display the data based on the available area size
  int firstHeight = 64;
  int lastHeight = 512;
  if (!isZoomable || (bankHeight != 0))
    firstHeight = lastHeight = bankHeight;

  for (int testVBanks = firstVBanks; testVBanks <= lastVBanks; testVBanks++)
  {
    if (bankCount % testVBanks)
      continue;

    for (int testHeight = firstHeight; testHeight <= lastHeight; testHeight *= 2)
    {
      const int testWidth = bankSize / testHeight;
      // Prefer the vertical rectangular bank aspect ratio (256 for a 4K bank)
      const int bankRatio = testHeight / testWidth;
      const bool preferred = (bankRatio >= 16) && (bankRatio <= 32);

      int testHBanks = bankCount / testVBanks;
      int testZoomLevel = 0;
      Common::Size testSize = MemViewWidget::calcSizeAndZoom(
        innerSurfaceW,  // availableWidth
        innerSurfaceH,  // availableHeight
        testWidth, // bankWidth
        testHeight, // bankHeight
        testHBanks,  // hBanks
        testVBanks,  // vBanks
        separators, // separators
        testZoomLevel // minZoomLevel
      );

      bool testFits = (testSize.w <= static_cast<uInt32>(innerSurfaceW)) && (testSize.h <= static_cast<uInt32>(innerSurfaceH));
      double testDiff = fabs(
        (static_cast<double>(testSize.w) / static_cast<double>(testSize.h))
        -
        destAspect
      );

      // Make the difference for our preferred bank aspect slightly better to prefer these
      // when there is very little difference between the arrangements
      if (preferred)
        testDiff *= 0.85;

      if (
        (bestAspectDiff < 0.0)
        ||
        (
          (!fits || testFits)
          &&
          (
            (!fits && testFits)
            ||
            (testZoomLevel > layoutParams.minZoomLevel)
            ||
            (testDiff < bestAspectDiff))
          )
        )
      {
        // Take new best option
        bestSize = testSize; 
        layoutParams.bankHeight = testHeight;
        layoutParams.bankWidth = testWidth;
        layoutParams.vBanks = testVBanks;
        layoutParams.hBanks = testHBanks;
        layoutParams.minZoomLevel = testZoomLevel;
        bestAspectDiff = testDiff;
        fits = testFits;
      }
    }
  }

  // At least one condition must be met
  assert(fits || isZoomable);

  if (layoutParams.vBanks == 1)
    singleRow = true;

  return {bestSize, layoutParams};
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::tuple<Common::Size, MemViewParams::LayoutParams> MemViewWidget::calcNeededSize(
  const GUI::Font& font, const int w, const int h, const bool isZoomable,
  uInt16 bankSize, uInt16 bankCount, const uInt16 initialBankHeight,
  bool& success,
  int* innerSurfaceW, int* innerSurfaceH, MemViewParams* params
)
{
  // Depending on if the pointers are set or not we are working on
  // external or internal variables
  int iSW, iSH;
  uInt16 bH;
  uInt32 dS;

  uInt16* bankHeight = nullptr;
  uInt32* dataSize = nullptr;

  if (innerSurfaceW == nullptr)
    innerSurfaceW = &iSW;
  if (innerSurfaceH == nullptr)
    innerSurfaceH = &iSH;

  if (params == nullptr)
  {
    bankHeight = &bH;
    dataSize = &dS;
    *dataSize = static_cast<uInt32>(bankSize * bankCount);
  }
  else
  {
    bankHeight = &params->myBankHeight;
    // Data size has already been calculated by MemViewParams
    dataSize = &params->myDataSize;
  }

  // Set maximal available inner surface dimensions
  *innerSurfaceW = w - 2 * FRAME_THICKNESS
    - (isZoomable ? (ScrollBarVWidget::scrollBarWidth(font) - FRAME_THICKNESS) : 0);
  *innerSurfaceH = h - 2 * FRAME_THICKNESS
    - (isZoomable ? (ScrollBarHWidget::scrollBarHeight(font) - FRAME_THICKNESS) : 0);

  // Evaluate bank height
  *bankHeight = static_cast<uInt16>(std::min(*dataSize, static_cast<uInt32>(initialBankHeight)));
  const bool fixedBankHeight = !isZoomable;

  // Check if bank is displayable
  success =
    (*dataSize > 0)
    &&
    (
      (
        (bankCount == 1)
        &&
        (
          fixedBankHeight
          ||
          ((bankSize % MemViewDialog::MAX_BANK_HEIGHT) == 0)
        )
      )
      ||
      (
        fixedBankHeight
        &&
        ((bankSize % *bankHeight) == 0)
      )
      ||
      (
        !fixedBankHeight
        &&
        ((bankSize % MemViewDialog::MAX_BANK_HEIGHT) == 0)
      )
    );

  if (!success)
  {
    Logger::error(std::format("MemViewWidget: Unsupported bank size: {}", bankSize));
    // Setup some emergency values to prevent crashing
    bankSize = DEFAULT_BANK_SIZE;
    bankCount = 1;
    *dataSize = DEFAULT_BANK_SIZE;
    if (params != nullptr)
    {
      params->myBankSize = DEFAULT_BANK_SIZE;
      params->myBankCount = 1;
    }
  }

  // Find the initial best layout to use (how are the banks arranged)
  // and return size and layout parameters
  auto [size, layoutParams] = findBestLayout(bankSize, bankCount, *innerSurfaceW, *innerSurfaceH,
    isZoomable, *bankHeight, false, true
  );

  if (success)
  {
    Logger::debug(std::format("Data size   = {}", *dataSize));
    Logger::debug(std::format("Banks       = {}", bankCount));
    Logger::debug(std::format("Bank size   = {}", bankSize));
    Logger::debug(std::format("Bank width  = {}", layoutParams.bankWidth));
    Logger::debug(std::format("Bank height = {}", layoutParams.bankHeight));
  }

  // Add the outer frame thickness and scrollbar sizes
  if (isZoomable)
  {
    size.w += FRAME_THICKNESS + ScrollBarVWidget::scrollBarWidth(font);
    size.h += FRAME_THICKNESS + ScrollBarHWidget::scrollBarHeight(font);
  }
  else
  {
    *innerSurfaceW = size.w;
    *innerSurfaceH = size.h;
    size.w += 2 * FRAME_THICKNESS;
    size.h += 2 * FRAME_THICKNESS;
  }

  return {size, layoutParams};
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
