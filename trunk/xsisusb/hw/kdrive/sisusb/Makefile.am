if DEBUG
DYNSYMS = @LD_EXPORT_SYMBOLS_FLAG@
endif

INCLUDES = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

noinst_LIBRARIES = libsisusb.a

bin_PROGRAMS = Xsisusb

libsisusb_a_SOURCES =	\
	sisusb.c		\
	sisusb_device.c		\
	kbd.c		\
	os.c		\
	mouse.c		\
	sisusb.h

Xsisusb_SOURCES = \
	sisusbinit.c

Xsisusb_LIBS =                     \
	libsisusb.a               \
	@KDRIVE_LIBS@		\
	@XSERVER_LIBS@

Xsisusb_LDADD = \
	$(Xsisusb_LIBS)            \
	$(TSLIB_FLAG)		\
	$(DYNSYMS)

