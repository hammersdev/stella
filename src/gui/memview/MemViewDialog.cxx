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

/*
  Cartridge support status:

  03E0      Breakpoints not working (Montezuma's Revenge) (Cartridge::bankOrigin), Stepping through code shows false reads at ROM end.
  0FA0      OK
  2K        OK
  3E        Breakpoints not working, no access data for cartridge RAM (Cartridge::bankOrigin!)
  3E+       Breakpoints not working, no access data for cartridge RAM (Cartridge::bankOrigin!)
  3EX       ?
  3F        Breakpoints not working for big ROMs (Bad Apple)
  4A50      No access data for internal RAM, program ROM only 4K, cart RAM not exposed
  4K        OK
  4KSC      OK
  0840      OK
  AR        Unsure - what is RAM/ROM? No access data
  BF        OK
  BFSC      OK
  BUS       Cart RAM without content, no access data for cart RAM, no access data for internal RAM 
  CDF       Cart RAM without content, no access data for cart RAM, no access data for internal RAM
  CDFJ+     Cart RAM without content, no access data for cart RAM, no access data for internal RAM
  CM        No access data for internal RAM
  CTY       Cart RAM not exposed
  CV        OK
  DevCard   ?
  DF        OK
  DFSC      OK
  DPC       OK
  DPC+      Cart RAM without content, PC in cart RAM (false addresses)
  E0        Breakpoints not always working
  E7        Cart RAM not exposed, Stepping through code shows false reads at ROM end.
  EF/EFF    Breakpoints not always working (invalid bank)
  EFSC      OK
  ELF       Only 4K of ROM(?) exposed
  F0        OK
  F4        OK
  F4SC      OK
  F6        OK
  F6SC      OK
  F8        OK
  F8SC      OK
  FA        OK
  FA2       OK
  FC        OK
  FE        OK
  GL        OK
  JANE      ?
  MDM       OK
  MVC       Unsupported
  SB        Breakpoints not always working (invalid bank)
  TVBoy     OK
  UA        OK
  WD        OK
  WF8       OK
  X07       No access data for internal RAM
*/

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
  calcColorDataTab(WRITE_COLOR_HIGH, WRITE_COLOR_MID, ROM_ALPHA_MAX, myRomWriteColorTab);
  calcColorDataTab(PC_COLOR_HIGH, PC_COLOR_MID, ROM_ALPHA_MAX, myRomPcColorTab);
  calcColorDataTab(READ_COLOR_HIGH, READ_COLOR_MID, RAM_ALPHA_MAX, myRamReadColorTab);
  calcColorDataTab(WRITE_COLOR_HIGH, WRITE_COLOR_MID, RAM_ALPHA_MAX, myRamWriteColorTab);
  calcColorDataTab(PC_COLOR_HIGH, PC_COLOR_MID, RAM_ALPHA_MAX, myRamPcColorTab);

  const int ramXPos = H_OUTER_BORDER;
  const int ramYPos = V_OUTER_BORDER;
  const int romYPos = V_OUTER_BORDER;
  const int settingsWidth = getSettingsWidth(font);
  const int settingsHeight = getSettingsHeight(font);
  const int ramMaxWidth = settingsWidth;
  const int ramMaxHeight = _h - 2 * V_OUTER_BORDER - settingsHeight - V_INNER_DIST;
  const int settingsXPos = H_OUTER_BORDER;
  const int settingsYPos = _h - V_OUTER_BORDER - settingsHeight;
  const int mainHeight = _h - 2 * V_OUTER_BORDER;
  const int colorsXPos = std::max(
    settingsXPos + CheckboxWidget::neededWidth(font, TEXT_LONGEST, boxSize) + H_TEXT_TO_WIDGET_DIST,
    settingsXPos + settingsWidth - COLOR_WIDGET_WIDTH
  );
  const int romTextWidth = font.getStringWidth(TEXT_ROM);

  // Place settings
  int ypos = settingsYPos;
  const int checkboxHeight = CheckboxWidget::neededHeight(font, boxSize);

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

  // Get the cartridge infos
  bool setupOk = true;
  bool singleRow = true;
  Cartridge &cart = instance().console().cartridge();

  // Determine ROM parameters
  const ByteSpan programRomContent = cart.getImage(Cartridge::ImageScope::PROGRAM);
  const uInt32 programRomSize = static_cast<uInt32>(programRomContent.size());
  uInt16 romBankSize = cart.bankSize();
  uInt16 romBankCount = cart.romBankCount();
  uInt32 romSize = romBankSize * romBankCount;

  Logger::debug(std::format("ROM size        = {}", programRomSize));
  Logger::debug(std::format("Calced ROM size = {} ({} x {})", romSize, romBankCount, romBankSize));

  // Check if the calculated size is bigger than the delivered one
  if (romSize > programRomSize)
  {
    if (programRomSize < romBankSize)
    {
      // Special case (e.g. for small CV ROMs)
      romBankCount = 1;
      romBankSize = programRomSize;
      romSize = programRomSize;
    }
    else
    {
      // For now: cut down size to full banks
      romBankCount = programRomSize / romBankSize;
      romSize = romBankSize * romBankCount;
    }
    Logger::debug(std::format("Corrected size  = {} ({} x {})", romSize, romBankCount, romBankSize));
  }
  // Limit content to the size we support
  ByteSpan romContent = ByteSpan(programRomContent.begin(), romSize);

  // Go through additional image scopes to add up byte
  // sizes and build a map of scopes needed at the main area
  struct MainAreaScope {
    MainAreaScope() : myBytes{0}, myWidth{0} { };
    MainAreaScope(uInt32 bytes, int width = 0) : myBytes{bytes}, myWidth{width} { }
    uInt32 myBytes;
    int myWidth;
  };
  std::map<Cartridge::ImageScope, MainAreaScope> mainAreaScopes;
  mainAreaScopes[Cartridge::ImageScope::PROGRAM] = MainAreaScope(romSize);
  uInt32 mainAreaLeftBytes = romSize;
  Cartridge::ImageScope largestScope = Cartridge::ImageScope::PROGRAM;
  for (
    Cartridge::ImageScope scope = extraScopeFirst;
    scope <= extraScopeLast;
    scope = Cartridge::ImageScope(std::to_underlying(scope) + 1)
  )
  {
    uInt32 extraBytes = static_cast<uInt32>(cart.getImage(scope).size());
    if (extraBytes > 0)
    {
      mainAreaLeftBytes += extraBytes;
      mainAreaScopes[scope] = MainAreaScope(extraBytes);
      if (extraBytes > mainAreaScopes[largestScope].myBytes)
        largestScope = scope;
    }
  }

  // Place RAM view and get dimensions
  int xpos = ramXPos;
  myRamView = new MemViewWidget(this, font, xpos, ramYPos, ramMaxWidth, ramMaxHeight,
    RAM_SIZE, 1, RAM_SIZE, false,
    myRamReadColorTab, myRamWriteColorTab, myRamPcColorTab,
    MemViewDataLayer::RAM_DATA_COLOR_DEFAULT, MemViewDataLayer::RAM_DATA_COLOR_FADED,
    "RAM", RAM_BASE
  );
  myViews.insert({Cartridge::ImageScope::NONE, myRamView});
  const int ramWidth = myRamView->getWidth();
  xpos += ramWidth + H_TEXT_TO_WIDGET_DIST;

  // Configure RAM view
  M6532& riot = instance().console().riot();
  myRamView->setAccessDataParams(riot.getRamCounterSize(), riot.getRamCounterOffset());

  // Place RAM text
  const int ramTextWidth = font.getStringWidth(TEXT_RAM);
  new StaticTextWidget(this, font, xpos, ramYPos, ramTextWidth,
    fontHeight, TEXT_RAM, TextAlign::Left
  );
  xpos += ramTextWidth;

  // Setup cartridge's RAM part (if any)
  const uInt32 cartRamSize = cart.internalRamSize();
  const uInt16 cartRamBankCount = cart.ramBankCount();
  Logger::debug(std::format("RAM bank count = {}", cartRamBankCount));
  Logger::debug(std::format("Int RAM size   = {}", cartRamSize));

  int mainAreaLeftSize = _w - H_OUTER_BORDER;
  int mainAreaNetLeftSize = mainAreaLeftSize - ramTextWidth - H_TEXT_TO_WIDGET_DIST -
    static_cast<int>(mainAreaScopes.size() - 1) * H_INNER_DIST;

  // Does the cartridge have internal RAM to be displayed?
  if (cartRamSize > 0)
  {
    if (cartRamSize <= 256)
    {
      // Smaller cartridge RAM will be shown next to the RIOT's RAM
      xpos += H_TEXT_TO_WIDGET_DIST;
      myCartRamView = new MemViewWidget(this, font, xpos, ramYPos,
        (cartRamSize <= 128) ? ramWidth : (ramWidth * 2),
        ramMaxHeight, cartRamSize, 1, RAM_SIZE, false,
        myRamReadColorTab, myRamWriteColorTab, myRamPcColorTab,
        MemViewDataLayer::RAM_DATA_COLOR_DEFAULT, MemViewDataLayer::RAM_DATA_COLOR_FADED,
        "Cart RAM", MemViewWidget::QUERY_RAM_BANK_ORIGIN
      );
      myViews.insert({Cartridge::ImageScope::NONE, myCartRamView});

      xpos += myCartRamView->getWidth() + H_INNER_DIST;
      xpos = std::max(xpos, settingsXPos + settingsWidth + H_INNER_DIST - H_TEXT_TO_WIDGET_DIST - romTextWidth);
      mainAreaLeftSize -= xpos;
      mainAreaNetLeftSize -= xpos;

      myCartRamView->setAccessDataParams(cart.getRamCounterSize(), cart.getRamCounterOffset());
    }
    else
    {
      // Big RAM is placed in the big GUI area before the ROM
      myBigCartRam = true;
      xpos += H_INNER_DIST;
      xpos = std::max(xpos, settingsXPos + settingsWidth + H_INNER_DIST - H_TEXT_TO_WIDGET_DIST - ramTextWidth);

      // Place RAM text
      new StaticTextWidget(this, font, xpos, ramYPos, ramTextWidth,
        fontHeight, TEXT_RAM, TextAlign::Left
      );
      xpos += ramTextWidth + H_TEXT_TO_WIDGET_DIST;
      mainAreaNetLeftSize -= xpos + H_INNER_DIST;

      double cartRamRatio = static_cast<double>(cartRamSize) / static_cast<double>(mainAreaLeftBytes + cartRamSize);
      int cartRamWidth = round(static_cast<double>(mainAreaNetLeftSize) * cartRamRatio);
      cartRamWidth = std::max(MIN_ROM_WIDTH, cartRamWidth);

      uInt16 bankCount = std::max((uInt16)1, cartRamBankCount);
      myCartRamView = new MemViewWidget(this, font, xpos, ramYPos, cartRamWidth,
        mainHeight, cartRamSize / bankCount, bankCount, 0, true,
        myRamReadColorTab, myRamWriteColorTab, myRamPcColorTab,
        MemViewDataLayer::DATA_COLOR_DEFAULT, MemViewDataLayer::DATA_COLOR_FADED,
        "Cart RAM", MemViewWidget::QUERY_RAM_BANK_ORIGIN
      );
      myViews.insert({Cartridge::ImageScope::NONE, myCartRamView});
      setupOk = setupOk && myCartRamView->isSetup();
      singleRow = singleRow && myCartRamView->lockedSingleRow();

      xpos += cartRamWidth + H_INNER_DIST;
      mainAreaLeftSize -= xpos;
      mainAreaNetLeftSize -= cartRamWidth;

      myCartRamView->setAccessDataParams(cart.getRamCounterSize(), cart.getRamCounterOffset());
    }
  }
  else
  {
    xpos += H_INNER_DIST;
    xpos = std::max(xpos, settingsXPos + settingsWidth + H_INNER_DIST - H_TEXT_TO_WIDGET_DIST - romTextWidth);
    mainAreaLeftSize -= xpos;
    mainAreaNetLeftSize -= xpos;
  }

  // Pre-calculate all ROM view GUI sizes
  if (mainAreaLeftBytes > 0)
  {
    for (auto &[scope, entry] : mainAreaScopes)
    {
      double ratio = static_cast<double>(entry.myBytes) / static_cast<double>(mainAreaLeftBytes);
      int width = round(static_cast<double>(mainAreaNetLeftSize) * ratio);
      int clampedWidth = std::max(MIN_ROM_WIDTH, width);
      int diffWidth = clampedWidth - width;
      // Subtract extra needs from the largest area
      if (diffWidth && (scope != largestScope))
      {
        // Take the extra needed space from the largest one
        mainAreaScopes[largestScope].myWidth -= diffWidth;
        entry.myWidth += clampedWidth;
      }
      else
      {
        entry.myWidth += width;
      }
    }
  }
  else
  {
    mainAreaScopes[Cartridge::ImageScope::PROGRAM].myWidth = mainAreaNetLeftSize;
  }

  // Setup cartridge's ROM part

  // Place ROM text
  new StaticTextWidget(this, font, xpos, romYPos,
    font.getStringWidth(TEXT_RAM), fontHeight, TEXT_ROM, TextAlign::Right
  );
  xpos += romTextWidth + H_TEXT_TO_WIDGET_DIST;

  // Instantiate and place all ROM views
  for (const auto &[scope, entry] : mainAreaScopes)
  {
    const int& width = entry.myWidth;
    MemViewWidget* newView = nullptr;

    switch (scope)
    {
      case Cartridge::ImageScope::PROGRAM:
      {
        const Settings& settings = instance().settings();
        int bankHeight = settings.getInt("mv.bankheight");
        if (bankHeight <= 0)
          bankHeight = 0;
        else if (bankHeight <= 64)
          bankHeight = 64;
        else if (bankHeight <= 128)
          bankHeight = 128;
        else if (bankHeight <= 256)
          bankHeight = 256;
        else
          bankHeight = 512;
        // Place program ROM view
        newView = new MemViewWidget(this, font, xpos, romYPos, width, mainHeight,
          romBankSize, romBankCount, bankHeight, true,
          myRomReadColorTab, myRomWriteColorTab, myRomPcColorTab,
          MemViewDataLayer::DATA_COLOR_DEFAULT, MemViewDataLayer::DATA_COLOR_FADED, "ROM",
          cart.getRomScopeOffset(scope) | MemViewWidget::QUERY_ROM_BANK_ORIGIN
        );
        // Set current content
        newView->updateData(romContent);
        break;
      }

      case Cartridge::ImageScope::DISPLAY_DATA:
      {
        // Place display data ROM view
        ByteSpan image = cart.getImage(scope);
        // TODO: check and adjust the image's size if necessary
        newView = new MemViewWidget(this, font, xpos, romYPos, width, mainHeight,
          static_cast<uInt16>(image.size()), 1, 0, true,
          myRomReadColorTab, myRomWriteColorTab, myRomPcColorTab,
          MemViewDataLayer::DATA_COLOR_DEFAULT, MemViewDataLayer::DATA_COLOR_FADED, "Display data",
          cart.getRomScopeOffset(scope) | MemViewWidget::QUERY_ROM_BANK_ORIGIN
        );
        // Set current content
        newView->updateData(image);
        break;
      }

      default:
        cerr << "Maybe you forgot something here?\n";
        continue;
    }

    if (newView != nullptr)
    {
      setupOk = setupOk && newView->isSetup();
      singleRow = singleRow && newView->lockedSingleRow();
      newView->setAccessDataParams(
        cart.getRomCounterSize(scope),
        cart.getRomCounterOffset(scope)
      );
      myViews.insert({scope, newView});
      xpos += width + H_INNER_DIST;
    }
  }

  // Check if ROM is supported and hide settings if unnecessary
  if (setupOk)
  {
    updateLayoutParameters();

    if (singleRow)
    {
      mySingleRow->setState(false);
      mySingleRow->setEnabled(false);
    }

    if ((romBankCount <= 1) && (cartRamBankCount <= 1))
    {
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

  // Setup rest
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
      for (const auto &[scope, view] : myViews)
        view->render();
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
  for (const auto &[scope, view] : myViews)
    view->updateRest();
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
  bool dirtyViews = false;
  for (const auto &[scope, view] : myViews)
    dirtyViews = dirtyViews || view->isDirty();
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

    case kSingleRowChanged:
    case kSeparatorsChanged:
      updateLayoutParameters();
      break;

    case kDecayRateChanged:
      for (const auto &[scope, view] : myViews)
        view->setDecayRate(myDecaySlider->getValue());
      break;

    case kClearButtonPressed:
      for (const auto &[scope, view] : myViews)
        view->clearHeatmaps();
      break;

    default:
      Dialog::handleCommand(sender, cmd, data, 0);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int MemViewDialog::getSettingsWidth(const GUI::Font& font)
{
  const int boxSize = CheckboxWidget::boxSize(font);
  return CheckboxWidget::neededWidth(font, TEXT_LONGEST, boxSize) +
    H_TEXT_TO_WIDGET_DIST * 2 + COLOR_WIDGET_WIDTH;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int MemViewDialog::getSettingsHeight(const GUI::Font& font)
{
  const int vGap = font.getFontHeight() / 4;
  const int boxSize = CheckboxWidget::boxSize(font);
  return CheckboxWidget::neededHeight(font, boxSize) * SETTINGS_COUNT
    + vGap * (SETTINGS_COUNT + 7 - 1);  // + 7 for the extra gaps
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
  for (const auto &[scope, view] : myViews)
  {
    view->setVisualParameters(
      myShowData->getState(),
      myShowPc->getState(),
      myShowReads->getState(),
      ((view == myRamView) || (view == myCartRamView))
        ? myShowWrites->getState() : false,
      myInverted->getState(),
      myByteFade->getState()
    );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewDialog::updateLayoutParameters()
{
  for (const auto &[scope, view] : myViews)
  {
    if (
      (view == myRamView)
      ||
      ((view == myCartRamView) && !myBigCartRam)
    )
      continue;

    view->setLayoutParameters(
      mySingleRow->getState(),
      mySeparators->getState()
    );
  }
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

  // Update ROM views
  for (const auto &[scope, view] : myViews)
  {
    if (scope == Cartridge::ImageScope::NONE)
      // These are the RAM views already handled above
      continue;

    // Update ROM accesses
    view->updateAccessData(
      cart.getRomDataPeekCounter(scope),
      cart.getRomPokeCounter(scope),
      cart.getRomCodePeekCounter(scope),
      cyclesDiff,
      elapsedFrames
    );
  }
}
