INCLUDES = 					\
	@KDRIVE_INCS@				\
	-I$(top_srcdir)/hw/kdrive/vesa		\
	@KDRIVE_CFLAGS@

bin_PROGRAMS = Xpm2

noinst_LIBRARIES = libpm2.a

libpm2_a_SOURCES = 	\
	glint_regs.h	\
	pm2.c		\
	pm2_draw.c	\
	pm2.h	

Xpm2_SOURCES = \
	pm2stub.c

PM2_LIBS =					\
	libpm2.a 				\
	$(top_builddir)/hw/kdrive/vesa/libvesa.a \
	@KDRIVE_LIBS@

Xpm2_LDADD = \
	$(PM2_LIBS)				\
	@KDRIVE_LIBS@                          \
	@XSERVER_LIBS@			
