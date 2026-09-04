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

#ifndef MEMVIEW_WIDGET_HXX
#define MEMVIEW_WIDGET_HXX

class ScrollBarVWidget;
class ScrollBarHWidget;
class FBSurface;
class ContextMenu;

#include "Variant.hxx"
#include "Device.hxx"
#include "Widget.hxx"
#include "Command.hxx"
#include "MemViewParams.hxx"
#include "MemViewDataLayer.hxx"
#include "MemViewAccessLayer.hxx"
#include "MemViewMarkerLayer.hxx"
#include <tuple>

/**
  This is the widget class for representing one complete data field (either RAM or ROM)
  with all it's layers (data, read, write, program counter).

  @author Christian Hammers
*/

class MemViewWidget : public Widget
{

  public:

    static constexpr std::string_view TEXT_UNSUPPORTED = "Unsupported ROM type";
    static constexpr int DEFAULT_BANK_SIZE = 4096;
    using ColorTab = std::array<uInt32, 256>;
    static constexpr uInt32 QUERY_ROM_BANK_ORIGIN = 1U << 30;
    static constexpr uInt32 QUERY_RAM_BANK_ORIGIN = 1U << 31;

  public:

    /**
      Constructor

      @param boss               Parent GUI object
      @param font               Font to use
      @param x                  X pos
      @param y                  Y pos
      @param w                  Initial width
      @param h                  Initial height
      @param bankSize           Size of one bank to represent (bytes)
      @param bankCount          Number of banks to represent (bytes)
      @param initialBankHeight  Initial bank height to use (in bytes) or zero if variable
      @param isZoomable         True when this view should be zoomable (also will have scrollbars)
      @param readColorTab       The 256 entry table with all the heatmap colors for read access data
      @param writeColorTab      The 256 entry table with all the heatmap colors for write access data
      @param pcColorTab         The 256 entry table with all the heatmap colors for PC access data
      @param dataDefaultColor   Default color for data bytes (not faded)
      @param dataFadedColor     Color to use when byte fade is active
      @param typeText           Short description of memory type to be shown in tool tip
      @param baseAddress        Base address of the data within the address range of the system
    */
    MemViewWidget(GuiObject *boss, const GUI::Font& font,
      const int x, const int y, const int w, const int h,
      const uInt16 bankSize, const uInt16 bankCount, uInt16 initialBankHeight,
      const bool isZoomable,
      const MemViewWidget::ColorTab& readColorTab,
      const MemViewWidget::ColorTab& writeColorTab,
      const MemViewWidget::ColorTab& pcColorTab,
      const uInt32 dataDefaultColor, uInt32 dataFadedColor,
      const string_view typeText,
      const uInt32 baseAddress
    );

    ~MemViewWidget() override;

    using Widget::setDirty;
    void setDirty(bool renderGui);

    /**
      Derived handle methods
    */
    void handleMouseDown(int x, int y, MouseButton b, int clickCount) override;
    void handleMouseUp(int x, int y, MouseButton b, int clickCount) override;
    void handleMouseWheel(int x, int y, int direction) override;
    void handleMouseMoved(int x, int y) override;
    void handleMouseLeft() override;
    bool wantsFocus() const override { return true; }

    string getToolTip(const Common::Point& pos) const override;
    bool changedToolTip(const Common::Point& oldPos,
      const Common::Point& newPos) const override { return true; }

    /**
      Set or update the data of this view to be displayed

      @param data   A ByteSpan pointing to the data
    */
    void updateData(const ByteSpan& data);

    /**
      Sets the size and position of the expected access data counters within our whole data range.

      @param size     Number of access counters to take over
      @param offset   Offset inside our whole data range to put the new data to

      @returns true if valid
    */
    bool setAccessDataParams(uInt32 size, uInt32 offset);

    /**
      Set or update new access counter data for the heatmaps.

      @param readAccessData     Pointer to the new read access data values to take over
      @param writeAccessData    Pointer to the new write access data values to take over
      @param pcAccessData       Pointer to the new program counter access data values to take over
      @param elapsedCycles      Number of CPU cycles elapsed since the last update
      @param elapsedFrames      Number of complete elapsed TV frames since last update.
    */
    void updateAccessData(
      Device::AccessCounter* readAccessData,
      Device::AccessCounter* writeAccessData,
      Device::AccessCounter* pcAccessData,
      const uInt32 elapsedCycles, const int elapsedFrames
    );

    /**
      Does all the rest what updateAccessData() didn't
    */
    void updateRest();

    /**
      Set new layout parameters

      @param singleRow    Only one row of banks
      @param separators   Show separators between the banks or not
    */
    void setLayoutParameters(bool singleRow, bool separators)
    { setLayoutParameters(myParams.myBankHeight, singleRow, separators); }

    /**
      Set new visual parameters

      @param showData     Enable/disable the visibility of the data bytes
      @param showPc       Enable/disable the visibility of the program counter heatmap
      @param showReads    Enable/disable the visibility of the data read heatmap
      @param showWrites   Enable/disable the visibility of the data write heatmap
      @param inverted     Select if the bytes should be shown inverted
      @param byteFade     Select if the bytes should fade from MSB to LSB
    */
    void setVisualParameters(bool showData, bool showPc, bool showReads, bool showWrites,
      bool inverted, bool byteFade);

    /**
      Set a new decay rate for the fading heatmap values 

      @param percantage   The value 0..100 (0 = stay forever, 100 = fade within one frame)
    */
    void setDecayRate(int percantage);

    /**
      Reset all heatmap values to 0
    */
    void clearHeatmaps();

    /**
      Read if only single row is possible with this bank configuration
    */
    bool lockedSingleRow();

    /**
      Read if it's only a single bank
    */
    bool lockedSingleBank();

    /**
      Returns if the widget is fully setup and functional after construction
    */
    bool isSetup() const { return myIsSetup; }

    /**
      Renders all layers to the backend.
    */
    void render();

    /**
      Calculates the needed size for the data to be shown.

      @param font               Reference to the font being used
      @param w                  Available width in pixels
      @param h                  Available height in pixels
      @param isZoomable         View should be zoomable or has fixed size
      @param bankSize           Size of one bank in bytes
      @param bankCount          Number of banks
      @param initialBankHeight  Bank height to use initially or 0 to automatically find best height
      @param success            Flag returning true on success
      @param innerSurfaceW      Optional pointer to receive the needed inner surface width
      @param innerSurfaceH      Optional pointer to receive the needed inner surface height
      @param params             Optional pointer to work on an already existing MemViewParams object

      @return Dimensions of determined display size and bank layout parameters
    */
    static std::tuple<Common::Size, MemViewParams::LayoutParams> calcNeededSize(
      const GUI::Font& font, const int w, const int h, const bool isZoomable,
      uInt16 bankSize, uInt16 bankCount, uInt16 initialBankHeight,
      bool& success,
      int* innerSurfaceW = nullptr,
      int* innerSurfaceH = nullptr,
      MemViewParams* params = nullptr
    );

  protected:

    bool hasToolTip() const override { return isSetup() && !myMouseDragging; }

    /**
      Draws the content of the widget if internal states indicate a redraw is necessary.
      Does not render to the backend.
    */
    virtual void drawWidget(bool hilite) override;

  private:

    static constexpr int FRAME_THICKNESS = 1;

    int myInnerSurfaceX{};
    int myInnerSurfaceY{};

    bool myIsZoomable;
    MemViewParams myParams;
    MemViewDataLayer myDataLayer;
    MemViewAccessLayer myReadLayer;
    MemViewAccessLayer myWriteLayer;
    MemViewAccessLayer myPcLayer;
    MemViewMarkerLayer myPcMarker;
    MemViewMarkerLayer myMouseMarker;

    string myTypeText;

    int myDataIsDirty{false};
    bool myIsSetup{false};
    bool mySkipNextUpdate{false};

    bool myMouseDragging{false};
    int myClickX{0};
    int myClickY{0};
    int myRightClickX{0};
    int myRightClickY{0};

    ScrollBarVWidget* myVScrollBar{nullptr};
    ScrollBarHWidget* myHScrollBar{nullptr};
    ContextMenu* myMenu{nullptr};

    std::vector<uInt8> myCurrentData;     // Holds the original data (not rearranged)
    shared_ptr<FBSurface> mySurface;

  private:

    /**
      Converts an external position within the whole GUI window to our internal
      surface position.
    */
    Common::Point extPosConv(const Common::Point& pos) const;

    /**
      Checks if an external position is within our shown data area.
    */
    bool extPosInData(const Common::Point& pos) const;

    /**
      Checks if an internal position is within our shown data area.
    */
    bool intPosInData(int x, int y) const;

    /**
      Get the items for the context menu with the current bankHeight selected
    */
    VariantList getContextMenuItems() const;

    /**
      Mark only our data bytes to be dirty and needs to be redrawn.
    */
    void setDirtyData();

    /**
      Clear the normal dirty flag as well as our dirty data flag
    */
    void clearEverythingDirty();

    /**
      Update our byte marker frame
    */
    void updateMarker();

  #ifdef IMAGE_SUPPORT
    void savePicture();
  #endif
    void handleCommand(CommandSender* sender, int cmd, int data, int id) override;

    /**
      Returns the resulting size in pixels for a specific memory view configuration
      and automatically determines minimum zoom level

      @param availableWidth    Total available width of area in pixel
      @param availableHeight   Total available height of area in pixels
      @param bankWidth         Width of one bank in bytes
      @param bankHeight        Height of one bank in bytes
      @param hBanks            Number of horizontally banks next to each other
      @param vBanks            Number of vertical stacked banks
      @param separators        Use separator lines inbetween the banks
      @param minZoomLevel      Reference to receive the minimum calculated zoom level

      @return Dimensions of suggested display size
    */
    static Common::Size calcSizeAndZoom(const int availableWidth, const int availableHeight,
      const int bankWidth, const int bankHeight, const int hBanks, const int vBanks,
      const bool separators, int& minZoomLevel);

    /**
      Finds the best layout for displaying the data on the available screen space given
      the user's preference for bankHeight (and therefore also the bankWidth) and if
      to show everything in a single row of banks or not.
      The layout arrangement is basically how many banks are shown next to each other
      and how many on top of each other (like 4x2 or 8x1 etc.).
      The best solution is determined by the highest zoom level or the best matching
      aspect ratio of the data block to the available screen space.

      @param bankSize       Size of one bank in bytes
      @param bankCount      Number of banks
      @param innerSurfaceW  Available width in pixels
      @param innerSurfaceH  Available height in pixels
      @param isZoomable     View should be zoomable or has fixed size
      @param bankHeight     Wanted bank height in bytes or 0 for auto-determine
      @param singleRow      Parameter to put in preference for single bank row
                            (But could be overwritten and returned differently)
      @param separators     Show separator lines inbetween the banks

      @return Dimensions of determined display size and bank layout parameters
    */
    static std::tuple<Common::Size, MemViewParams::LayoutParams> findBestLayout(
      const uInt16& bankSize, const uInt16& bankCount,
      const int& innerSurfaceW, const int& innerSurfaceH,
      const bool& isZoomable, const uInt16 bankHeight,
      bool singleRow, const bool separators
    );

    /**
      Copies the heatmap data of all layers to the display fields in one go.
      Note: This could have been done in the MemViewAccessLayer class for each layer
      separately but it's a little bit faster to only once go through the rearrangement loop.

      @param force    Forces to update the data regardless if they are currently visible or not
    */
    void heatmapsToFields(bool force = false);

    void recalcScrollBars();

    /**
      Change the zoom level for the displayed data
    */
    void zoom(int level);

    /**
      Set new layout parameters

      @param bankHeight   Height of a bank in bytes
      @param singleRow    Only one row of banks
      @param separators   Show separators between the banks or not
    */
    void setLayoutParameters(uInt16 bankHeight, bool singleRow, bool separators);

  private:
    // Following constructors and assignment operators not supported
    MemViewWidget() = delete;
    MemViewWidget(const MemViewWidget&) = delete;
    MemViewWidget(MemViewWidget&&) = delete;
    MemViewWidget& operator=(const MemViewWidget&) = delete;
    MemViewWidget& operator=(MemViewWidget&&) = delete;
};

#endif  // MEMVIEW_WIDGET_HXX
