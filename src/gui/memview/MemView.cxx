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

#include "MemViewDialog.hxx"
#include "Version.hxx"
#include "OSystem.hxx"
#include "Settings.hxx"
#include "MemViewFrameBuffer.hxx"
#include "bspf.hxx"
#include "Console.hxx"
#include "System.hxx"
#include "MemView.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemView::MemView(OSystem& osystem, FrameBuffer& framebuffer)
  : DialogContainer(osystem, framebuffer),
    mySize{myOSystem.settings().getSize("mv.res")}
{
  MemViewDialog::setMinSize(mySize, framebuffer.font());

  myOSystem.settings().setValue("mv.res", mySize);

  myBaseDialog = new MemViewDialog(myOSystem, *this, framebuffer.font(), 0, 0, mySize.w, mySize.h);
  myBaseDialog->open();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MemView::~MemView()
{
  delete myBaseDialog;  myBaseDialog = nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus MemView::initializeVideo()
{
  return myOSystem.memViewFrameBuffer().createDisplay(MEMVIEW_TITLE,
    BufferType::MemView, mySize);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Dialog* MemView::baseDialog()
{
  return myBaseDialog;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MemView::setForcedUpdate(bool forced)
{
  myBaseDialog->setForcedUpdate(forced);
}
