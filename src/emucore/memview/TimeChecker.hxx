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

#ifndef TIME_CHECKER_HXX
#define TIME_CHECKER_HXX

// Just a dumb class to measure times (to be removed)

#include "bspf.hxx"
#include <chrono>
#include <string_view>

class TimeChecker
{

  using TimeStamp = std::chrono::time_point<std::chrono::steady_clock>;

  public:
    TimeChecker(std::string_view name, bool print = true) : name{name}, print{print}
    {
    }

    ~TimeChecker()
    {
    }

    void start(bool reset = false)
    {
      startTime = std::chrono::steady_clock::now();
      if (reset)
      {
        count = 0;
        sum = 0;
      }
    }

    uInt64 stop(TimeChecker* other = nullptr, bool suppressPrint = false)
    {
      TimeStamp endTime = std::chrono::steady_clock::now();
      count++;
      lastInterval = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
      sum += lastInterval;
      if (other)
      {
        uInt64 otherInterval = other->lastInterval;
        cout << name << ": " << (sum / count) << " avg. (now = " << lastInterval << "), "
          << other->name << ": " << (other->sum / other->count) << " avg. (now = " << otherInterval << "), "
          << "Sum: " << (lastInterval + otherInterval) << "\n" << std::flush;
      }
      else if (print && !suppressPrint)
      {
        cout << name << ": " << (sum / count) << " avg. (now = " << lastInterval << ")\n" << std::flush;
      }
      return lastInterval;
    }

    void reset()
    {
      count = 0;
      sum = 0;
    }

    std::string_view name;
    bool print;
    uInt64 count{0};
    uInt64 sum{0};
    uInt64 lastInterval{0};
    TimeStamp startTime;

  private:
    // Following constructors and assignment operators not supported
    TimeChecker() = delete;
    TimeChecker(const TimeChecker&) = delete;
    TimeChecker(TimeChecker&&) = delete;
    TimeChecker& operator=(const TimeChecker&) = delete;
    TimeChecker& operator=(TimeChecker&&) = delete;

};

#endif  // TIME_CHECKER_HXX
