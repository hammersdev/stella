MODULE := src/emucore/memview

MODULE_OBJS := \
        src/emucore/memview/MemViewFrameBuffer.o

MODULE_TEST_OBJS =

MODULE_DIRS += \
        src/emucore/memview

# Include common rules
include $(srcdir)/common.rules
