include $(top_srcdir)/cpprules.in

if DRI
DRI_SUBDIR = dri
endif

if XF86UTILS
XF86UTILS_SUBDIR = utils
endif

if MFB
MFB_SUBDIR = xf1bpp xf4bpp
endif

if CFB
CFB_SUBDIR = xf8_16bpp xf8_32bpp
endif

DOC_SUBDIR = doc

SUBDIRS = common ddc dummylib i2c x86emu int10 fbdevhw os-support parser rac \
	  ramdac shadowfb vbe vgahw xaa $(MFB_SUBDIR) $(CFB_SUBDIR) \
	  loader scanpci dixmods exa modes \
	  $(DRI_SUBDIR) $(XF86UTILS_SUBDIR) $(DOC_SUBDIR)

DIST_SUBDIRS = common ddc dummylib i2c x86emu int10 fbdevhw os-support \
               parser rac ramdac shadowfb vbe vgahw xaa xf1bpp xf4bpp \
               xf8_16bpp xf8_32bpp loader scanpci dixmods dri exa modes \
	       utils doc

bin_PROGRAMS = Xorg

AM_CFLAGS = $(DIX_CFLAGS) @XORG_CFLAGS@
INCLUDES = @XORG_INCS@ 

Xorg_SOURCES = xorg.c

# libxorgos and libcommon need symbols from each other
noinst_LTLIBRARIES = libosandcommon.la
libosandcommon_la_SOURCES = osandcommon.c
libosandcommon_la_LIBADD = \
            os-support/libxorgos.la \
            common/libcommon.la

osandcommon.c xorg.c:
	touch $@

DISTCLEANFILES = osandcommon.c xorg.c

XORG_LIBS = \
            @XORG_CORE_LIBS@ \
            common/libinit.a \
            loader/libloader.a \
            libosandcommon.la \
	    rac/librac.a \
            parser/libxf86config.a \
	    dixmods/libdixmods.la \
	    modes/libxf86modes.a \
	    ramdac/libramdac.a \
	    ddc/libddc.a \
	    i2c/libi2c.a \
            @XORG_LIBS@

Xorg_DEPENDENCIES = \
	$(XORG_LIBS)

Xorg_LDADD = $(XORG_LIBS) \
             @XSERVER_LIBS@ \
	     dixmods/libxorgxkb.la

Xorg_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

BUILT_SOURCES = xorg.conf.example
CLEAN = xorg.conf.example xorg.conf.example.pre
EXTRA_DIST = xorgconf.cpp

if SOLARIS_ASM_INLINE
# Needs to be built before any files are compiled when using Sun compilers
# so in*/out* inline definitions are properly processed.

BUILT_SOURCES += os-support/solaris/solaris-$(SOLARIS_INOUT_ARCH).il

os-support/solaris/solaris-$(SOLARIS_INOUT_ARCH).il:
	cd os-support/solaris ; make solaris-$(SOLARIS_INOUT_ARCH).il
endif

# do not use $(mkdir_p) if you want automake 1.7 to work
install-data-local:
	mkdir -p $(DESTDIR)$(logdir)


install-exec-local: install-binPROGRAMS
	(cd $(DESTDIR)$(bindir) && rm -f X && ln -s Xorg X)
if INSTALL_SETUID
	chown root $(DESTDIR)$(bindir)/Xorg
	chmod u+s $(DESTDIR)$(bindir)/Xorg
endif

optionsdir = $(libdir)/X11
dist_options_DATA = Options

CPP_FILES_FLAGS = \
	-DRGBPATH=\"$(RGB_DB)\" \
	-DLOCALFONTPATH="\"$(BASE_FONT_PATH)/local\"" \
	-DMISCFONTPATH="\"$(BASE_FONT_PATH)/misc\"" \
	-DT1FONTPATH="\"$(BASE_FONT_PATH)/Type1\"" \
	-DTRUETYPEFONTPATH="\"$(BASE_FONT_PATH)/TTF\"" \
	-DDPI75FONTPATH="\"$(BASE_FONT_PATH)/75dpi\"" \
	-DDPI100FONTPATH="\"$(BASE_FONT_PATH)/100dpi\"" \
	-DMODULEPATH=\"$(DEFAULT_MODULE_PATH)\"

relink:
	rm -f Xorg && $(MAKE) Xorg

xorg.conf.example.pre: xorgconf.cpp
	cp $(srcdir)/xorgconf.cpp $@
