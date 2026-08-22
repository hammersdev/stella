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
#include "OSystem.hxx"
#include "Widget.hxx"
#include "Font.hxx"
#include "MediaFactory.hxx"
#include "ScrollBarHWidget.hxx"
#include "ScrollBarVWidget.hxx"
#include "PopUpWidget.hxx"
#include "ColorWidget.hxx"
#include "Console.hxx"
#include "Cart.hxx"
#include "M6532.hxx"
#include "TIA.hxx"
#include "System.hxx"
#include "Logger.hxx"
#include "MemViewWidget.hxx"
#include "MemViewDialog.hxx"

#define COLORMIX50(a, b) \
  ((((a & 0xFF0000) + (b & 0xFF0000)) / 2) & 0xFF0000) | \
  ((((a & 0xFF00) + (b & 0xFF00)) / 2) & 0xFF00) | \
  ((((a & 0xFF) + (b & 0xFF)) / 2) & 0xFF)

#define COLORMIX33(a, b) \
  ((((a & 0xFF0000) / 3) + ((b & 0xFF0000) * 2 / 3)) & 0xFF0000) | \
  ((((a & 0xFF00) / 3) + ((b & 0xFF00) * 2 / 3)) & 0xFF00) | \
  ((((a & 0xFF) / 3) + ((b & 0xFF) * 2 / 3)) & 0xFF)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewDialog::MemViewDialog(OSystem& osystem, DialogContainer& parent,
  const GUI::Font& font, int x, int y, int w, int h
)
  : Dialog(osystem, parent, x, y, w, h)
{
  const int fontHeight   = Dialog::fontHeight(),
            fontWidth    = Dialog::fontWidth(),
            VGAP         = Dialog::vGap();
  const int boxSize = CheckboxWidget::boxSize(font);
  WidgetArray wid;

  // Build middle value between high and mid colors for the three ColorWidgets
  static constexpr uInt32 readWidgetColor = COLORMIX33(READ_COLOR_HIGH, READ_COLOR_MID) | 0xFF000000;
  static constexpr uInt32 writeWidgetColor = COLORMIX33(WRITE_COLOR_HIGH, WRITE_COLOR_MID) | 0xFF000000;
  static constexpr uInt32 pcWidgetColor = COLORMIX33(PC_COLOR_HIGH, PC_COLOR_MID) | 0xFF000000;
  // (The alpha values set to full are needed for Direct3D 11, which sometimes seems
  // to ignore the blend setting of the surface)

  // Calculate our color tables
  calcColorDataTab(READ_COLOR_HIGH, READ_COLOR_MID, ROM_ALPHA_MAX, myRomReadColorTab);
//  calcColorDataTab(WRITE_COLOR_HIGH, WRITE_COLOR_MID, ROM_ALPHA_MAX, myRomWriteColorTab);
  calcColorDataTab(PC_COLOR_HIGH, PC_COLOR_MID, ROM_ALPHA_MAX, myRomPcColorTab);
  calcColorDataTab(READ_COLOR_HIGH, READ_COLOR_MID, RAM_ALPHA_MAX, myRamReadColorTab);
  calcColorDataTab(WRITE_COLOR_HIGH, WRITE_COLOR_MID, RAM_ALPHA_MAX, myRamWriteColorTab);
  calcColorDataTab(PC_COLOR_HIGH, PC_COLOR_MID, RAM_ALPHA_MAX, myRamPcColorTab);

  const int ramXPos = H_OUTER_BORDER;
  const int ramYPos = V_OUTER_BORDER;
  const int settingsWidth = getSettingsWidth(font);
  const int settingsHeight = getSettingsHeight(font);
  const int ramMaxWidth = settingsWidth;
  const int ramMaxHeight = _h - 2 * V_OUTER_BORDER - settingsHeight - V_INNER_DIST;
  const int settingsXPos = H_OUTER_BORDER;
  const int settingsYPos = _h - V_OUTER_BORDER - settingsHeight;

  // Place RAM view and get dimensions
  myRamView = new MemViewWidget(this, font, ramXPos, ramYPos, ramMaxWidth, ramMaxHeight,
    RAM_SIZE, 1, RAM_SIZE, false, true, false,
    myRamReadColorTab, myRamWriteColorTab, myRamPcColorTab,
    MemViewDataLayer::RAM_DATA_COLOR_DEFAULT, MemViewDataLayer::RAM_DATA_COLOR_FADED,
    "RAM", RAM_BASE
  );
  myRamView->setLayoutParameters(true, false, RAM_SIZE);
  M6532& riot = instance().console().riot();
  myRamView->setAccessDataParams(riot.getRamCounterSize(), riot.getRamCounterOffset());

  const int ramWidth = myRamView->getWidth();
  int ramTotalWidth = ramWidth + H_TEXT_TO_WIDGET_DIST + font.getStringWidth(TEXT_RAM);

  // Get the cartridge infos
  Cartridge &cart = instance().console().cartridge();

  ByteSpan fullRomContent = cart.getImage();
  size_t fullRomSize = fullRomContent.size();

  uInt16 romBankSize = cart.bankSize();
  uInt16 romBankCount = cart.romBankCount();
  uInt32 romSize = romBankSize * romBankCount;

  if (romSize > fullRomSize)
  {
    // For now: cut down size to full banks
    romBankCount = fullRomSize / romBankSize;
    romSize = romBankSize * romBankCount;
  }
  ByteSpan romContent = ByteSpan(fullRomContent.begin(), romSize);

  uInt32 cartRamSize = cart.internalRamSize();
  uInt16 cartRamBankCount = cart.ramBankCount();
  uInt32 extraRomSize = fullRomSize - romSize;
  Logger::debug(std::format("Full ROM size  = {}", fullRomSize));
  Logger::debug(std::format("Bank size      = {}", romBankSize));
  Logger::debug(std::format("ROM bank count = {}", romBankCount));
  Logger::debug(std::format("Extra ROM size = {}", extraRomSize));
  Logger::debug(std::format("RAM bank count = {}", cartRamBankCount));
  Logger::debug(std::format("Int RAM size   = {}", cartRamSize));

  // Does the cartridge have internal RAM to be displayed?
  if (cartRamSize != 0)
  {
    if (cartRamSize <= 256)
    {
      // Smaller cartridge RAM will be shown next to the RIOT's RAM
      const int cartRamXPos = ramXPos + ramTotalWidth + H_TEXT_TO_WIDGET_DIST;
      myCartRamView = new MemViewWidget(this, font, cartRamXPos, ramYPos, ramWidth * 2,
        ramMaxHeight, cartRamSize, 1, RAM_SIZE, false, true, false,
        myRamReadColorTab, myRamWriteColorTab, myRamPcColorTab,
        MemViewDataLayer::RAM_DATA_COLOR_DEFAULT, MemViewDataLayer::RAM_DATA_COLOR_FADED,
        "Cart RAM", ROM_BASE, abs(cart.getRamMirrorAddrDiff())
      );
      myCartRamView->setLayoutParameters(true, false, RAM_SIZE);
      myCartRamView->setAccessDataParams(cart.getRamCounterSize(), cart.getRamCounterOffset());
      ramTotalWidth += H_INNER_DIST + myCartRamView->getWidth();
    }
  }

  // Calculate ROM position
  const int romTextWidth = font.getStringWidth(TEXT_ROM);
  int romTextXPos = std::max(ramTotalWidth + H_INNER_DIST + H_TEXT_TO_WIDGET_DIST,
    settingsXPos + getSettingsWidth(font) + H_INNER_DIST - H_TEXT_TO_WIDGET_DIST - romTextWidth);
  myRomXPos = romTextXPos + romTextWidth + H_TEXT_TO_WIDGET_DIST;
  myRomYPos = V_OUTER_BORDER;
  myRomWidth = _w - myRomXPos - H_OUTER_BORDER;
  myRomHeight = _h - 2 * V_OUTER_BORDER;
  const int colorsXPos = std::max(
    settingsXPos + CheckboxWidget::neededWidth(font, TEXT_LONGEST, boxSize) + H_TEXT_TO_WIDGET_DIST,
    settingsXPos + settingsWidth - COLOR_WIDGET_WIDTH
  );

  // Place settings
  int ypos = settingsYPos;
  const int checkboxHeight = CheckboxWidget::neededHeight(font, boxSize);

  VariantList bankHeights;
  VarList::push_back(bankHeights, "64");
  VarList::push_back(bankHeights, "128");
  VarList::push_back(bankHeights, "256");
  VarList::push_back(bankHeights, "512");
  myBankHeight = new PopUpWidget(this, font, settingsXPos, ypos, 
    font.getStringWidth(bankHeights[1].first),checkboxHeight, bankHeights, TEXT_BANK_HEIGHT,
    font.getStringWidth(TEXT_BANK_HEIGHT) + H_TEXT_TO_WIDGET_DIST, kBankHeightChanged
  );
  myBankHeight->setSelectedIndex(1);
  ypos += checkboxHeight + VGAP * 4;

  mySingleRow = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SINGLE_ROW, kSingleRowChanged);
  wid.push_back(mySingleRow);
  ypos += checkboxHeight + VGAP;

  mySeparators = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SEPARATORS, kSeparatorsChanged);
  wid.push_back(mySeparators);
  ypos += checkboxHeight + VGAP;

  myInverted = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_INVERTED, kInvertedChanged);
  wid.push_back(myInverted);
  ypos += checkboxHeight + VGAP;

  myByteFade = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_BYTE_FADE, kByteFadeChanged);
  wid.push_back(myByteFade);
  ypos += checkboxHeight + VGAP * 3;

  myShowData = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SHOW_DATA, kShowDataChanged);
  wid.push_back(myShowData);
  ypos += checkboxHeight + VGAP;

  myShowPc = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SHOW_PC, kShowPcChanged);
  wid.push_back(myShowPc);
  myPcColor = new ColorWidget(this, font, colorsXPos, ypos, COLOR_WIDGET_WIDTH, boxSize, 0, true);
  myPcColor->setColorRgb(pcWidgetColor);
  ypos += checkboxHeight + VGAP;

  myShowReads = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SHOW_READS, kShowReadsChanged);
  wid.push_back(myShowReads);
  myReadColor = new ColorWidget(this, font, colorsXPos, ypos, COLOR_WIDGET_WIDTH, boxSize, 0, true);
  myReadColor->setColorRgb(readWidgetColor);
  ypos += checkboxHeight + VGAP;

  myShowWrites = new CheckboxWidget(this, font, settingsXPos, ypos, TEXT_SHOW_WRITES, kShowWritesChanged);
  wid.push_back(myShowWrites);
  myWriteColor = new ColorWidget(this, font, colorsXPos, ypos, COLOR_WIDGET_WIDTH, boxSize, 0, true);
  myWriteColor->setColorRgb(writeWidgetColor);
  ypos += checkboxHeight + VGAP * 3;

  new StaticTextWidget(this, font, settingsXPos,
    ypos, font.getStringWidth(TEXT_DECAY_RATE), fontHeight, TEXT_DECAY_RATE, TextAlign::Left
  );
  ypos += checkboxHeight + VGAP;

  myDecaySlider = new SliderWidget(this, font, settingsXPos, ypos, settingsWidth - fontWidth * 5,
    checkboxHeight, "", 0, kDecayRateChanged, fontWidth * 4, "%", fontWidth
  );
  myDecaySlider->setMinValue(0);
  myDecaySlider->setMaxValue(100);
  myDecaySlider->setStepValue(1);
  wid.push_back(myDecaySlider);
  ypos += checkboxHeight + VGAP * 4;

  myClearButton = new ButtonWidget(this, font, settingsXPos, ypos,
    settingsWidth, checkboxHeight, TEXT_CLEAR, kClearButtonPressed
  );

  // Place ROM view
  myRomView = new MemViewWidget(this, font, myRomXPos, myRomYPos, myRomWidth, myRomHeight,
    romBankSize, romBankCount, 256, true, false, true, 
    myRomReadColorTab, myRomWriteColorTab, myRomPcColorTab, 
    MemViewDataLayer::DATA_COLOR_DEFAULT, MemViewDataLayer::DATA_COLOR_FADED, "ROM", ROM_BASE
  );
  myRomView->setAccessDataParams(cart.getRomCounterSize(), cart.getRomCounterOffset());

  // Check if ROM is supported and corresponding view is setup correctly
  if (myRomView->isSetup())
  {
    // Set current content
    myRomView->updateData(romContent);
    updateLayoutParameters();

    if (myRomView->lockedSingleRow())
    {
      mySingleRow->setState(false);
      mySingleRow->setEnabled(false);
    }

    if (romBankCount <= 1) {
      // No separators if one bank only
      mySeparators->setState(false);
      mySeparators->setEnabled(false);
    }
  }
  else
  {
    // Setup error
    // Disable corresponding GUI elements
    mySingleRow->setState(false);
    mySingleRow->setEnabled(false);
    mySeparators->setState(false);
    mySeparators->setEnabled(false);
  }

  // Place RAM text
  new StaticTextWidget(this, font, ramXPos + ramWidth + H_TEXT_TO_WIDGET_DIST,
    ramYPos, font.getStringWidth(TEXT_RAM), fontHeight, TEXT_RAM, TextAlign::Left
  );

  // Place ROM text
  new StaticTextWidget(this, font, romTextXPos, myRomYPos,
    font.getStringWidth(TEXT_RAM), fontHeight, TEXT_ROM, TextAlign::Right
  );

  addToFocusList(wid);

  myLastCycles = instance().console().system().cycles();
  myLastFrames = instance().console().tia().frameCount();
  myLastHadFrameWrap = false;

  // Add a callback for rendering our extra surfaces at the correct time
  // (after the base GUI surface has been rendered and before any overlaying
  // ContextMenus or similar)
  addRenderCallback(
    [this]()
    {
      myRamView->render();
      if (myCartRamView)
        myCartRamView->render();
      myRomView->render();
    }
  );

  // Let the layers copy the initial state of access counters
  updateAccessData();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewDialog::~MemViewDialog()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::loadConfig()
{
  const Settings& settings = instance().settings();

  myBankHeight->setSelectedIndex(settings.getInt("mv.bankheight"));
  if (mySingleRow->isEnabled())
    mySingleRow->setState(settings.getBool("mv.singlerow"));
  myInverted->setState(settings.getBool("mv.inverted"));
  myByteFade->setState(settings.getBool("mv.bytefade"));
  if (mySeparators->isEnabled())
    mySeparators->setState(settings.getBool("mv.separators"));
  myShowData->setState(settings.getBool("mv.showdata"));
  myShowPc->setState(settings.getBool("mv.showpc"));
  myShowReads->setState(settings.getBool("mv.showreads"));
  myShowWrites->setState(settings.getBool("mv.showwrites"));
  myDecaySlider->setValue(settings.getInt("mv.decayrate"));

  updateLayoutParameters();
  updateVisualParameters();
 }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::tick()
{
  Dialog::tick();
  // Get elapsed cycles
  const uInt64 currentCycles = instance().console().system().cycles();
  uInt64 cyclesDiff = 0;
  // No rewind supported
  // TODO: the uInt64 wrap-around won't work like this
  // but will anyone of us will be still around when this happens?
  if (currentCycles > myLastCycles)
    cyclesDiff = currentCycles - myLastCycles;
  myLastCycles = currentCycles;

  if ((cyclesDiff != 0) && (cyclesDiff <= UINT32_MAX))
  {
    // Evaluate frame counts
    uInt32 currentFrames = instance().console().tia().frameCount();
    const bool hadFrameWrap = (currentFrames != myLastFrames);
    // We consider the time which has passed since the last call a full frame
    // if both this and the last call had different total frame counts
    const bool fullFrames = myLastHadFrameWrap && hadFrameWrap;
    const int elapsedFrames = fullFrames ? (currentFrames - myLastFrames) : 0;
    myLastFrames = currentFrames;
    myLastHadFrameWrap = hadFrameWrap;

    updateAccessData(static_cast<uInt32>(cyclesDiff), elapsedFrames);
  }

  // Update misc stuff
  myRamView->updateRest();
  if (myCartRamView)
    myCartRamView->updateRest();
  myRomView->updateRest();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::setMinSize(Common::Size& size, const GUI::Font& font)
{
  const uInt32 minW = H_OUTER_BORDER * 2 + getSettingsWidth(font) + H_INNER_DIST +
    MIN_ROM_WIDTH + ScrollBarVWidget::scrollBarWidth(font);
  const uInt32 minH = V_OUTER_BORDER * 2 + getSettingsHeight(font) + V_INNER_DIST +
    MIN_RAM_HEIGHT;
  size.w = std::max(size.w, minW);
  size.h = std::max(size.h, minH);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool MemViewDialog::needsRedraw()
{
  bool dirtyGui = isDirty() || isChainDirty();
  bool dirtyViews =
    myRamView->isDirty()
    ||
    (myCartRamView ? myCartRamView->isDirty() : false)
    ||
    myRomView->isDirty();

  if (!myForcedUpdate && dirtyViews && !dirtyGui)
    skipBaseUpdate();

  return dirtyGui || dirtyViews;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::handleCommand(CommandSender* sender, int cmd, int data, int id)
{
  switch(cmd)
  {
    case kInvertedChanged:
    case kByteFadeChanged:
    case kShowDataChanged:
    case kShowPcChanged:
    case kShowReadsChanged:
    case kShowWritesChanged:
      updateVisualParameters();
      break;

    case kBankHeightChanged:
    case kSingleRowChanged:
    case kSeparatorsChanged:
      updateLayoutParameters();
      break;

    case kDecayRateChanged:
      myRamView->setDecayRate(myDecaySlider->getValue());
      if (myCartRamView)
        myCartRamView->setDecayRate(myDecaySlider->getValue());
      myRomView->setDecayRate(myDecaySlider->getValue());
      break;

    case kClearButtonPressed:
      myRamView->clearHeatmaps();
      if (myCartRamView)
        myCartRamView->clearHeatmaps();
      myRomView->clearHeatmaps();
      break;

    default:
      Dialog::handleCommand(sender, cmd, data, 0);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int MemViewDialog::getSettingsWidth(const GUI::Font& font)
{
  return font.getStringWidth("512") + font.getStringWidth(TEXT_BANK_HEIGHT) +
    H_TEXT_TO_WIDGET_DIST + PopUpWidget::dropDownWidth(font);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int MemViewDialog::getSettingsHeight(const GUI::Font& font)
{
  const int vGap = font.getFontHeight() / 4;
  const int boxSize = CheckboxWidget::boxSize(font);
  return CheckboxWidget::neededHeight(font, boxSize) * SETTINGS_COUNT
    + vGap * (SETTINGS_COUNT + 10 - 1);  // + 10 for the extra gaps
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Calculate color table as such:
// 0..127   rising alpha value of the colorMid
// 128      colorMid with full alpha (this is the color to be used when access counter diff == 1
// 129..255 color sweep from colorMid to colorHigh
void MemViewDialog::calcColorDataTab(uInt32 colorHigh, uInt32 colorMid, const double alphaMax,
  MemViewWidget::ColorTab& tab)
{
  // Precalc color data table with alpha channel
  colorHigh &= 0xFFFFFF;
  colorMid &= 0xFFFFFF;

  static constexpr int middle = 128;

  const double rHigh = static_cast<double>((colorHigh >> 16) & 0xFF);
  const double gHigh = static_cast<double>((colorHigh >> 8) & 0xFF);
  const double bHigh = static_cast<double>(colorHigh & 0xFF);
  const double rMid = static_cast<double>((colorMid >> 16) & 0xFF);
  const double gMid = static_cast<double>((colorMid >> 8) & 0xFF);
  const double bMid = static_cast<double>(colorMid & 0xFF);
  const double steps = static_cast<double>(255 - middle);
  const double rStep = (rHigh - rMid) / steps;
  const double gStep = (gHigh - gMid) / steps;
  const double bStep = (bHigh - bMid) / steps;
  double r = rHigh;
  double g = gHigh;
  double b = bHigh;

  for (int i = 255; i >= middle; i--)
  {
    tab[i] = (static_cast<uInt32>(round(alphaMax)) << 24) |
      (static_cast<uInt32>(round(r)) << 16) |
      (static_cast<uInt32>(round(g)) << 8) |
      static_cast<uInt32>(round(b));
    r -= rStep;
    g -= gStep;
    b -= bStep;
  }

  double alpha = alphaMax;
  const double alphaStep = alphaMax / static_cast<double>(middle);

  // Just go down with the alpha value
  for (int i = middle - 1; i >= 0; i--)
  {
    alpha -= alphaStep;
    tab[i] = (static_cast<uInt32>(round(alpha)) << 24) | colorMid;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::updateVisualParameters()
{
  myRamView->setVisualParameters(
    myShowData->getState(),
    myShowPc->getState(),
    myShowReads->getState(),
    myShowWrites->getState(),
    myInverted->getState(),
    myByteFade->getState()
  );

  if (myCartRamView)
  {
    myCartRamView->setVisualParameters(
      myShowData->getState(),
      myShowPc->getState(),
      myShowReads->getState(),
      myShowWrites->getState(),
      myInverted->getState(),
      myByteFade->getState()
    );
  }

  myRomView->setVisualParameters(
    myShowData->getState(),
    myShowPc->getState(),
    myShowReads->getState(),
    false,
    myInverted->getState(),
    myByteFade->getState()
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::updateLayoutParameters()
{
  const int bankHeight = std::stoi(myBankHeight->getSelectedName());
  myRomView->setLayoutParameters(
    mySingleRow->getState(),
    mySeparators->getState(),
    bankHeight
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::updateAccessData(uInt32 cyclesDiff, int elapsedFrames)
{
  // Update RAM data
  M6532& riot = instance().console().riot();
  Cartridge &cart = instance().console().cartridge();

  // Update RAM data and accesses
  myRamView->updateData(riot.getRAM());
  myRamView->updateAccessData(
    riot.getRamDataPeekCounter(),
    riot.getRamPokeCounter(),
    riot.getRamCodePeekCounter(),
    cyclesDiff,
    elapsedFrames
  );

  // Update cartridge RAM data and accesses
  if (myCartRamView)
  {
    myCartRamView->updateData(cart.getRAM());
    myCartRamView->updateAccessData(
      cart.getRamDataPeekCounter(),
      cart.getRamPokeCounter(),
      cart.getRamCodePeekCounter(),
      cyclesDiff,
      elapsedFrames
    );
  }

  // Update ROM accesses
  myRomView->updateAccessData(
    cart.getRomDataPeekCounter(),
    cart.getRomPokeCounter(),
    cart.getRomCodePeekCounter(),
    cyclesDiff,
    elapsedFrames
  );
}
