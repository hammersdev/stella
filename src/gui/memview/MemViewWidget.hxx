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

#include "Device.hxx"
#include "Widget.hxx"
#include "Command.hxx"
#include "MemViewParams.hxx"
#include "MemViewDataLayer.hxx"
#include "MemViewAccessLayer.hxx"
#include "MemViewMarkerLayer.hxx"

/**
  This is the widget class for representing one complete data field (either RAM or ROM)
  with all it's layers (data, read, write, program counter).

  @author Christian Hammers
*/

class MemViewWidget : public Widget
{

  public:

    static constexpr std::string_view TEXT_UNSUPPORTED = "Unsupported ROM type";
    static constexpr int MAX_BANK_SIZE = 4096;
    using ColorTab = std::array<uInt32, 256>;

  public:

    /**
      Constructor

      @param boss               Parent GUI object
      @param font               Font to use
      @param x                  X pos
      @param y                  Y pos
      @param w                  Initial width
      @param h                  Initial height
      @param dataSize           Size of the data to represent (bytes)
      @param bankHeight         Initial bank height to use (in bytes) - may change later
      @param isZoomable         True when this view should be zoomable (also will have scrollbars)
      @param singleRow          Initial single row setting (all banks are in one row next each other)
      @param separators         Use separators by default
      @param readColorTab       The 256 entry table with all the heatmap colors for read access data
      @param writeColorTab      The 256 entry table with all the heatmap colors for write access data
      @param pcColorTab         The 256 entry table with all the heatmap colors for PC access data
      @param dataDefaultColor   Default color for data bytes (not faded)
      @param dataFadedColor     Color to use when byte fade is active
      @param baseAddress        Base address of the data within the address range of the system
    */
    MemViewWidget(GuiObject *boss, const GUI::Font& font,
      int x, int y, int w, int h,
      int dataSize, int bankHeight, bool isZoomable, bool singleRow, bool separators, 
      MemViewWidget::ColorTab& readColorTab,
      MemViewWidget::ColorTab& writeColorTab,
      MemViewWidget::ColorTab& pcColorTab,
      uInt32 dataDefaultColor, uInt32 dataFadedColor,
      uInt16 baseAddress
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
      Set or update new access counter data for the heatmaps

      @param readAccessData     Pointer to the new read access data values to take over
      @param writeAccessData    Pointer to the new write access data values to take over
      @param pcAccessData       Pointer to the new program counter access data values to take over
      @param size               Size of each data set (must be the same for all)
      @param elapsedCycles      Number of CPU cycles elapsed since the last update
      @param elapsedFrames      Number of complete elapsed TV frames since last update.
    */
    void updateAccessData(
      Device::AccessCounter* readAccessData,
      Device::AccessCounter* writeAccessData,
      Device::AccessCounter* pcAccessData,
      const uInt32 size, const uInt32 elapsedCycles, const int elapsedFrames
    );

    /**
      Does all the rest what updateAccessData() didn't
    */
    void updateRest();

    /**
      Set new layout parameters

      @param singleRow    Only one row of banks
      @param separators   Show separators between the banks or not
      @param bankHeight   Height of a bank in bytes
    */
    void setLayoutParameters(bool singleRow, bool separators, int bankHeight);

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
      Read if only single row is possible with this bank size
    */
    bool lockedSingleRow();

    /**
      Returns if the widget is fully setup and functional after construction
    */
    bool isSetup() const { return myIsSetup; }

    /**
      Renders all layers to the backend.
    */
    void render();

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
    int myInnerSurfaceW{0};
    int myInnerSurfaceH{0};

    bool myIsZoomable;
    MemViewParams myParams;
    MemViewDataLayer myDataLayer;
    MemViewAccessLayer myReadLayer;
    MemViewAccessLayer myWriteLayer;
    MemViewAccessLayer myPcLayer;
    MemViewMarkerLayer myPcMarker;
    MemViewMarkerLayer myMouseMarker;

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
    static Common::Size calcSizeAndZoom(int availableWidth, int availableHeight, int bankWidth,
      int bankHeight, int hBanks, int vBanks, bool separators, int& minZoomLevel);

    /**
      Finds the best layout for displaying the data on the available screen space given
      the user's preference for bankHeight (and therefore also the bankWidth) and if
      to show everything in a single row of banks or not.
      The layout arrangement is basically how many banks are shown next to each other
      and how many on top of each other (like 4x2 or 8x1 etc.).
      The best solution is determined by the highest zoom level or the best matching
      aspect ratio of the data block to the available screen space.

      @param bankWidth      Width of one bank in bytes
      @param bankHeight     Height of one bank in bytes
      @param separators     Show separator lines inbetween the banks
      @param singleRow      Parameter to put in preference for single bank row
                            (But could be overwritten and returned differently)
      @param hBanks         Reference to get back the number of horizontal banks
      @param vBanks         Reference to get back the number of vertical banks
      @param minZoomLevel   Reference to get back the minimum zoom level 

      @return Dimensions of determined display size
    */
    Common::Size findBestLayout(const int& bankWidth, const int& bankHeight,
      const bool& separators, bool& singleRow, int& hBanks, int& vBanks, 
      int& minZoomLevel);

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

  private:
    // Following constructors and assignment operators not supported
    MemViewWidget() = delete;
    MemViewWidget(const MemViewWidget&) = delete;
    MemViewWidget(MemViewWidget&&) = delete;
    MemViewWidget& operator=(const MemViewWidget&) = delete;
    MemViewWidget& operator=(MemViewWidget&&) = delete;
};

#endif  // MEMVIEW_WIDGET_HXX
