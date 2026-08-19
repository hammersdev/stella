MODULE := src/gui/memview

MODULE_OBJS := \
        src/gui/memview/MemView.o \
        src/gui/memview/MemViewDialog.o \
        src/gui/memview/MemViewWidget.o \
				src/gui/memview/MemViewParams.o \
				src/gui/memview/MemViewLayer.o \
				src/gui/memview/MemViewDataLayer.o \
				src/gui/memview/MemViewAccessLayer.o \
				src/gui/memview/MemViewMarkerLayer.o

MODULE_TEST_OBJS =

MODULE_DIRS += \
        src/gui/memview

# Include common rules
include $(srcdir)/common.rules
