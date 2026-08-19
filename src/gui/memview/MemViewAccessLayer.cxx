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

#include "FBSurface.hxx"
#include "MemViewAccessLayer.hxx"
#include "MemViewParams.hxx"
#include <cmath>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewAccessLayer::MemViewAccessLayer(Dialog& dialog, MemViewParams &params,
  ColorTab& colorTab
) : MemViewLayer(dialog, params, false),
    myColorTab{colorTab}
{
  myHeatmap.assign(myParams.myDataSize, 0.0);
  myFields.assign(myParams.myDataSize, 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemViewAccessLayer::~MemViewAccessLayer()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::draw()
{
  // Draw if visible
  if (myVisibility)
  {
    // Begin drawing on output layer data
    uInt32* destAddr = nullptr;
    uInt32 pitchWords = 0;
    if (!beginDraw(destAddr, pitchWords))
      return;

    drawImpl(myParams, myFields, destAddr, pitchWords);

    // Close drawing for this layer
    endDraw();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::render()
{
  // Draw if visible
  if (myVisibility)
    renderImpl();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::updateAccessData(Device::AccessCounter* accessData, 
  const MemViewAccessLayer::HeatmapValue& currentDecrement,
  const int elapsedFrames
)
{
  // Compare new data with last one and update our heatmap accordingly
  if (myLastAccessData.empty() || mySkipNextUpdate)
  {
    // First call - take over the data
    myLastAccessData.assign(accessData, accessData + myParams.myDataSize);
    if (!mySkipNextUpdate)
      myStartAccessData.assign(myParams.myDataSize, 0);
    mySkipNextUpdate = false;
  }
  else
    compareAccessData(accessData, currentDecrement, elapsedFrames);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::clearHeatmap()
{
  std::fill(myHeatmap.begin(), myHeatmap.end(), 0.0);
  myStartAccessData = myLastAccessData;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::loadTotals(bool skipNextUpdate)
{
  mySkipNextUpdate = skipNextUpdate;

  uInt64 count = 0;
  uInt64 sum = 0;
  for (const auto& value : myLastAccessData)
  {
    if (value != 0)
    {
      sum += value;
      count++;
    }
  }

  if (count == 0)
    return;

  const double average = static_cast<double>(sum) / static_cast<double>(count);
  const double gain = static_cast<double>((255.0 - BASE_ACCESS_VALUE + 1.0) / 2.0) / average;
  for (int i = 0; i < myParams.myDataSize; i++)
  {
    const Device::AccessCounter& accessValue = myLastAccessData[i];
    if (accessValue != 0) {
      HeatmapValue value = BASE_ACCESS_VALUE + static_cast<HeatmapValue>(round(static_cast<double>(accessValue - 1) * gain));
      if (value > 255.0)
        value = 255.0;
      myHeatmap[i] = value;
    }
    else
      myHeatmap[i] = 0;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Device::AccessCounter MemViewAccessLayer::getTotalValue(const unsigned int offset) const
{
  return (offset >= myLastAccessData.size()) ? 0 : myLastAccessData[offset];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Device::AccessCounter MemViewAccessLayer::getDeltaValue(const unsigned int offset) const
{
  return (offset >= myLastAccessData.size()) ? 0 :
    (myLastAccessData[offset] - myStartAccessData[offset]);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemViewAccessLayer::compareAccessData(Device::AccessCounter* newData,
  const MemViewAccessLayer::HeatmapValue& currentDecrement, const int elapsedFrames)
{
  std::vector<Device::AccessCounter>& oldData = myLastAccessData;
  std::vector<HeatmapValue>& heatMap = myHeatmap;
  double gain = myHeatmapGain;

  // If more than one frame has passed since last update, scale down the gain
  // for this round
  if (elapsedFrames > 1)
    gain /= elapsedFrames;

  uInt32 sum = 0;
  uInt32 count = 0;

  for (int i = 0; i < myParams.myDataSize; i++)
  {
    if (newData[i] != oldData[i])
    {
      // Calc diff and take over
      uInt32 diff = newData[i] - oldData[i];
      oldData[i] = newData[i];

      // Do the stats for gain control
      count++;
      sum += diff;

      // Apply gain and calc heatmap value
      HeatmapValue value = BASE_ACCESS_VALUE + static_cast<HeatmapValue>(diff - 1) * gain;
      if (value > 255.0)
        value = 255.0;

      if (value > heatMap[i])
      {
        heatMap[i] = value;
      }
      else
      {
        // At least decrement
        if ((heatMap[i] - currentDecrement) < value)
          heatMap[i] = value;
        else
          heatMap[i] -= currentDecrement;
      }

    }
    else
    {
      // Decrement because nothing new happened at this address
      if (heatMap[i] <= currentDecrement)
        heatMap[i] = 0;
      else
        heatMap[i] -= currentDecrement;
    }
  }

  // Calculate new gain
  if ((elapsedFrames == 1) && count)
  {
    const double average = static_cast<double>(sum) / static_cast<double>(count);
    const double thisGain = static_cast<double>((255.0 - BASE_ACCESS_VALUE + 1.0) / 2.0) / average;
    // 1/3 from the old, 2/3 from the new
    myHeatmapGain = myHeatmapGain / 3.0 + thisGain * 2.0 / 3.0;
    // Limit to some value
    if (myHeatmapGain > 64.0)
      myHeatmapGain = 64.0;
  }
}
