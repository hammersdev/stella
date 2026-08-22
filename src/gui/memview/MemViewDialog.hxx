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

#ifndef MEMVIEW_DIALOG_HXX
#define MEMVIEW_DIALOG_HXX

class OSystem;
class DialogContainer;
class CommandSender;
class StaticTextWidget;
class PopUpWidget;
class ColorWidget;
class SliderWidget;

#include "Dialog.hxx"
#include "MemViewWidget.hxx"

/**
  The main dialog for all Memory View functionalities and displays.

  @author  Christian Hammers
*/

class MemViewDialog : public Dialog
{
  public:

    static constexpr uInt32 READ_COLOR_HIGH = 0xc9c5ff;
    static constexpr uInt32 READ_COLOR_MID = 0x302abc;

    static constexpr uInt32 WRITE_COLOR_HIGH = 0xffb2f1;
    static constexpr uInt32 WRITE_COLOR_MID = 0x9c2782;

    static constexpr uInt32 PC_COLOR_HIGH = 0xfff094;
    static constexpr uInt32 PC_COLOR_MID = 0xbfa62b;

    static constexpr int FRAME_THICKNESS = 1;
    static constexpr int H_OUTER_BORDER = 32;
    static constexpr int V_OUTER_BORDER = 24;
    static constexpr int H_INNER_DIST = 32;
    static constexpr int V_INNER_DIST = 24;
    static constexpr int H_TEXT_TO_WIDGET_DIST = 8;
    static constexpr int RAM_SIZE = 128;
    static constexpr uInt16 RAM_BASE = 0x80;
    static constexpr uInt16 ROM_BASE = 0x1000;
    static constexpr int MIN_RAM_HEIGHT = RAM_SIZE * 2;
    static constexpr int MIN_ROM_WIDTH = 256;
    static constexpr int MAX_BANK_HEIGHT = 512;
    static constexpr int COLOR_WIDGET_WIDTH = 32;
    static constexpr double ROM_ALPHA_MAX = 196.0;
    static constexpr double RAM_ALPHA_MAX = 128.0;

    static constexpr std::string_view TEXT_RAM = "RAM";
    static constexpr std::string_view TEXT_ROM = "ROM";
    static constexpr std::string_view TEXT_BANK_HEIGHT = "Bank height:";
    static constexpr std::string_view TEXT_SINGLE_ROW = "Single row";
    static constexpr std::string_view TEXT_SEPARATORS = "Separators";
    static constexpr std::string_view TEXT_INVERTED = "Inverted";
    static constexpr std::string_view TEXT_BYTE_FADE = "Byte fade";
    static constexpr std::string_view TEXT_SHOW_DATA = "Show data";
    static constexpr std::string_view TEXT_SHOW_PC = "Show PC";
    static constexpr std::string_view TEXT_SHOW_READS = "Show reads";
    static constexpr std::string_view TEXT_SHOW_WRITES = "Show writes";
    static constexpr std::string_view TEXT_DECAY_RATE = "Decay rate:";
    static constexpr std::string_view TEXT_CLEAR = "Clear";
    static constexpr int SETTINGS_COUNT = 12;
    static constexpr std::string_view TEXT_LONGEST = TEXT_SHOW_WRITES;

    enum {
      kBankHeightChanged      = 'BHch',
      kSingleRowChanged       = 'RSch',
      kSeparatorsChanged      = 'Sech',
      kInvertedChanged        = 'Inch',
      kByteFadeChanged        = 'BFch',
      kShowDataChanged        = 'SDch',
      kShowPcChanged          = 'SPch',
      kShowReadsChanged       = 'SRch',
      kShowWritesChanged      = 'SWch',
      kDecayRateChanged       = 'DRch',
      kClearButtonPressed     = 'CBpr'
    };

  public:

    MemViewDialog(OSystem& osystem, DialogContainer& parent,
      const GUI::Font& font, int x, int y, int w, int h);

    ~MemViewDialog() override;

    void loadConfig() override;

    void tick() override;

    /**
      Updates the passed size struct to the minimum required dialog dimensions if necessary.

      @param size    The size to be checked and updated
      @param font    The used font on this dialog
    */
    static void setMinSize(Common::Size& size, const GUI::Font& font);

    /**
      Overriding the default needsRedraw method to be able to distinguish between
      draws needes by the base GUI and draws needed by our extra MemViewWidget layers.
    */
    bool needsRedraw() override;

    /**
      Set flag if the rendering is forced or not to skip any planned
      rendering optimizations.

      @param forced   Flag selecting if forced or not
    */
    void setForcedUpdate(bool forced) { myForcedUpdate = forced; }

  private:

    void handleCommand(CommandSender* sender, int cmd, int data, int id) override;

    // Returns the needed pixel width for the MemView's settings
    static int getSettingsWidth(const GUI::Font& font);
    // Returns the needed pixel height for the MemView's settings
    static int getSettingsHeight(const GUI::Font& font);
    // Calculate color gradient based on a bright (high) color and the normal (mid) one to the tab
    void calcColorDataTab(uInt32 colorHigh, uInt32 colorMid, const double alphaMax,
                          MemViewWidget::ColorTab& tab);
    // Updates the visual parameters if changes have been made to the corresponding settings
    void updateVisualParameters();
    // Updates the layout parameters if changes have been made to the corresponding settings
    void updateLayoutParameters();
    // Update all views with the current access data
    void updateAccessData(uInt32 cyclesDiff = 0, int elapsedFrames = 0);

  private:

    PopUpWidget*      myBankHeight{nullptr};
    CheckboxWidget*   mySingleRow{nullptr};
    CheckboxWidget*   myInverted{nullptr};
    CheckboxWidget*   myByteFade{nullptr};
    CheckboxWidget*   mySeparators{nullptr};
    CheckboxWidget*   myShowData{nullptr};
    CheckboxWidget*   myShowPc{nullptr};
    CheckboxWidget*   myShowReads{nullptr};
    CheckboxWidget*   myShowWrites{nullptr};
    SliderWidget*     myDecaySlider{nullptr};
    ButtonWidget*     myClearButton{nullptr};

    ColorWidget*      myPcColor{nullptr};
    ColorWidget*      myReadColor{nullptr};
    ColorWidget*      myWriteColor{nullptr};

    MemViewWidget*    myRamView{nullptr};
    MemViewWidget*    myRomView{nullptr};
    MemViewWidget*    myCartRamView{nullptr};

    MemViewWidget::ColorTab myRamReadColorTab;  // ARGB
    MemViewWidget::ColorTab myRamWriteColorTab; // ARGB
    MemViewWidget::ColorTab myRamPcColorTab;  // ARGB

    MemViewWidget::ColorTab myRomReadColorTab;  // ARGB
    MemViewWidget::ColorTab& myRomWriteColorTab{myRomReadColorTab}; // currently not needed
    MemViewWidget::ColorTab myRomPcColorTab;  // ARGB

    bool myForcedUpdate{false};
    int myRomXPos{0};
    int myRomYPos{0};
    int myRomWidth{0};
    int myRomHeight{0};

    uInt64 myLastCycles{0};
    uInt32 myLastFrames{0};
    bool myLastHadFrameWrap{false};

  private:
    // Following constructors and assignment operators not supported
    MemViewDialog() = delete;
    MemViewDialog(const MemViewDialog&) = delete;
    MemViewDialog(MemViewDialog&&) = delete;
    MemViewDialog& operator=(const MemViewDialog&) = delete;
    MemViewDialog& operator=(MemViewDialog&&) = delete;
};

#endif  // MEMVIEW_DIALOG_HXX
