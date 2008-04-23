if XV
XV_SRCS = omap_video.c omap_video_formats.c
endif

if DEBUG
DYNSYMS = @LD_EXPORT_SYMBOLS_FLAG@
endif

INCLUDES =                      \
	@KDRIVE_INCS@               \
	@KDRIVE_CFLAGS@

bin_PROGRAMS = Xomap

noinst_LIBRARIES = libomap.a

libomap_a_SOURCES =   \
	omap.c        \
	omap.h        \
	omap_draw.c   \
	omap_plane.c  \
	$(XV_SRCS)

Xomap_SOURCES = \
	omapinit.c

OMAP_LIBS =                     \
	libomap.a               \
	@KDRIVE_LIBS@		\
	@XSERVER_LIBS@

Xomap_LDADD = \
	$(OMAP_LIBS)            \
	$(TSLIB_FLAG)		\
	$(DYNSYMS)

EXTRA_DIST = omap_video.c
