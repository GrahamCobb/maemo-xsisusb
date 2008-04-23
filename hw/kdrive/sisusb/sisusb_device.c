/*
 * $Id$
 *
 * Copyright © 2008 Graham Cobb

   Portions of this program are derived from the sisbit image display program
   Copyright (C) 2005 Helmut Dersch <der@fh-furtwangen.de>

   Portions of this program are derived from the sisusb-driver 
   Copyright (C) 2005 by Thomas Winischhofer


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "sisusb.h"
#include <sys/ioctl.h>

#define SISUSB_PCI_PSEUDO_MEMBASE   	0x10000000
#define SISUSB_PCI_IOPORTBASE	        0x0000d000
#define SISUSB_PCI_PSEUDO_IOPORTBASE	0x0000d000

/* graphics core related */

#define AROFFSET	0x40
#define ARROFFSET	0x41
#define GROFFSET	0x4e
#define SROFFSET	0x44
#define CROFFSET	0x54
#define MISCROFFSET	0x4c
#define MISCWOFFSET	0x42
#define INPUTSTATOFFSET 0x5A
#define PART1OFFSET	0x04
#define PART2OFFSET	0x10
#define PART3OFFSET	0x12
#define PART4OFFSET	0x14
#define PART5OFFSET	0x16
#define CAPTUREOFFSET	0x00
#define VIDEOOFFSET	0x02
#define COLREGOFFSET	0x48
#define PELMASKOFFSET	0x46
#define VGAENABLE	0x43

#define SISAR		SISUSB_PCI_IOPORTBASE + AROFFSET
#define SISARR		SISUSB_PCI_IOPORTBASE + ARROFFSET
#define SISGR		SISUSB_PCI_IOPORTBASE + GROFFSET
#define SISSR		SISUSB_PCI_IOPORTBASE + SROFFSET
#define SISCR		SISUSB_PCI_IOPORTBASE + CROFFSET
#define SISMISCR	SISUSB_PCI_IOPORTBASE + MISCROFFSET
#define SISMISCW	SISUSB_PCI_IOPORTBASE + MISCWOFFSET
#define SISINPSTAT	SISUSB_PCI_IOPORTBASE + INPUTSTATOFFSET
#define SISPART1	SISUSB_PCI_IOPORTBASE + PART1OFFSET
#define SISPART2	SISUSB_PCI_IOPORTBASE + PART2OFFSET
#define SISPART3	SISUSB_PCI_IOPORTBASE + PART3OFFSET
#define SISPART4	SISUSB_PCI_IOPORTBASE + PART4OFFSET
#define SISPART5	SISUSB_PCI_IOPORTBASE + PART5OFFSET
#define SISCAP		SISUSB_PCI_IOPORTBASE + CAPTUREOFFSET
#define SISVID		SISUSB_PCI_IOPORTBASE + VIDEOOFFSET
#define SISCOLIDXR	SISUSB_PCI_IOPORTBASE + COLREGOFFSET - 1
#define SISCOLIDX	SISUSB_PCI_IOPORTBASE + COLREGOFFSET
#define SISCOLDATA	SISUSB_PCI_IOPORTBASE + COLREGOFFSET + 1
#define SISCOL2IDX	SISPART5
#define SISCOL2DATA	SISPART5 + 1
#define SISPEL		SISUSB_PCI_IOPORTBASE + PELMASKOFFSET
#define SISVGAEN	SISUSB_PCI_IOPORTBASE + VGAENABLE
#define SISDACA		SISCOLIDX
#define SISDACD		SISCOLDATA

#define SUCMD_GET      0x01	/* for all: data0 = index, data3 = port */
#define SUCMD_SET      0x02	/* data1 = value */
#define SUCMD_SETOR    0x03	/* data1 = or */
#define SUCMD_SETAND   0x04	/* data1 = and */
#define SUCMD_SETANDOR 0x05	/* data1 = and, data2 = or */
#define SUCMD_SETMASK  0x06	/* data1 = data, data2 = mask */

#define SUCMD_CLRSCR   0x07	/* data0:1:2 = length, data3 = address */

#define SISUSB_COMMAND		_IOWR(0xF3,0x3D,struct sisusb_command)

struct screendata{
    int width, height;
    unsigned char VCLK_a, VCLK_b;
    char crtcdata[17];
};


      //          HTot,HDEE,HBlS,HBlE,HReS,HReE,VTot,Ovfl
      //          VReS,VReE,VDEE,VBlS,VBlE,xHOV,xVO1,xVO2
      //          xPTC
struct screendata sd[] = {
    { 640, 480, 0x1b,  0xe1,                               // Mode 0: 25MHz
                { 0x5f,0x4f,0x4f,0x83,0x55,0x81,0x0b,0x3e, // SiSUSB_CRT1Table[5] (640x480)
		  0xe9,0x8b,0xdf,0xe8,0x0c,0x00,0x00,0x05,
		  0x00 }},
    { 800, 600, 0xc3,  0xc8,                               // Mode 3, 36MHz: 
                { 0x7b,0x63,0x63,0x9f,0x6a,0x93,0x6f,0xf0, // SiSUSB_CRT1Table[0xd] (800x600)
		  0x58,0x8a,0x57,0x57,0x70,0x20,0x00,0x05,
		  0x01 }},
    {1024, 768, 0x29, 0x61,                                // Mode f, 75MHz: 
               { 0xa1,0x7f,0x7f,0x85,0x86,0x97,0x24,0xf5, // SiSUSB_CRT1Table[0x17] (1024x768)
   		  0x02,0x88,0xff,0xff,0x25,0x10,0x00,0x02,
   		  0x01 }},
    {1280,1024, 0x70, 0x44,                                // Mode 19
                { 0xce,0x9f,0x9f,0x92,0xa9,0x17,0x28,0x5a, // SiSUSB_CRT1Table[0x1C] (1280x1024)
   		  0x00,0x83,0xff,0xff,0x29,0x09,0x00,0x07,
   		  0x01 }}};

int sisusb_setmode(KdScreenInfo *screen, struct screendata *mode, int bpp);

void
sisusbResendFrame (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    SiSusbScrPriv	*scrpriv = screen->driver;
    SiSusbPriv		*priv = screen->card->driver;

    lseek( priv->sisusbfd, SISUSB_PCI_PSEUDO_MEMBASE, SEEK_SET);
    ssize_t ret = write(priv->sisusbfd, priv->base, priv->bytes_per_line * screen->height);
    if (ret != priv->bytes_per_line * screen->height) {
      fprintf(stderr, "Write to device returned %d\n", ret);
      perror("Error writing buffer to device");
      abort();
    }
    fprintf(stderr,".");
}

void
sisusbOpenDevice (KdScreenInfo *screen)
{
    SiSusbPriv		*priv = screen->card->driver;
    int bpp=0;
    char* sisusb_device = "/dev/sisusbvga0";

    priv->sisusbfd = open(sisusb_device, O_RDWR);
    if (priv->sisusbfd < 0) {
      perror("Error acessing USB dongle");
      exit(1);
    }
    
    /* Hardcode mode to 1024x768 */
    if ( screen->fb[0].bitsPerPixel == 2*8 ) bpp=2;
    else if ( screen->fb[0].bitsPerPixel == 4*8 ) bpp=4;

    if ( (screen->width != 1024) || (screen->height != 768) || (bpp == 0) ) {
      fprintf(stderr, "Invalid mode (%dx%dx%d/%d): mode must be 1024x768x24/32 or 1024x768x16/16, 32\n", screen->width, screen->height, screen->fb[0].depth, screen->fb[0].bitsPerPixel);
      exit(2);
    }

    sisusb_setmode(screen, &sd[2], screen->fb[0].bitsPerPixel/8);
}

void
sisusbCloseDevice (KdScreenInfo *screen)
{
    SiSusbPriv		*priv = screen->card->driver;
    close(priv->sisusbfd);
}

typedef unsigned char __u8;
typedef unsigned int __u32;

struct sisusb_command {
	__u8   operation;	/* see below */
	__u8   data0;		/* operation dependent */
	__u8   data1;		/* operation dependent */
	__u8   data2;		/* operation dependent */
	__u32  data3;		/* operation dependent */
	__u32  data4;		/* for future use */
};

#define GETIREG(r,i,d) {\
      struct sisusb_command y; \
      y.data3 = r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE;\
      y.data0 = i;\
      y.data1 = *( d );\
      y.operation = SUCMD_GET;\
      ioctl(priv->sisusbfd, SISUSB_COMMAND, &y);\
      *( d ) = y.data1;\
      }

#define SETIREG(r,i,d) {\
      struct sisusb_command y; \
      y.data3 = r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE;\
      y.data0 = i;\
      y.data1 = d;\
      y.operation = SUCMD_SET;\
      ioctl(priv->sisusbfd, SISUSB_COMMAND, &y);\
      }

#define SETIREGOR(r,i,d) {\
      struct sisusb_command y; \
      y.data3 = r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE;\
      y.data0 = i;\
      y.data1 = d;\
      y.operation = SUCMD_SETOR;\
      ioctl(priv->sisusbfd, SISUSB_COMMAND, &y);\
      }

#define SETIREGAND(r,i,d) {\
      struct sisusb_command y; \
      y.data3 = r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE;\
      y.data0 = i;\
      y.data1 = d;\
      y.operation = SUCMD_SETAND;\
      ioctl(priv->sisusbfd, SISUSB_COMMAND, &y);\
      }
#define SETIREGANDOR(r,i,d,o) {\
      struct sisusb_command y; \
      y.data3 = r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE;\
      y.data0 = i;\
      y.data1 = d;\
      y.data2 = o;\
      y.operation = SUCMD_SETANDOR;\
      ioctl(priv->sisusbfd, SISUSB_COMMAND, &y);\
      }

#define SETREG(r,d) {\
      unsigned char b8 = d;\
      lseek( priv->sisusbfd, r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE, SEEK_SET);\
      write(priv->sisusbfd, &b8, 1);}
      
#define GETREG(r,d) {\
      unsigned char b8;\
      lseek( priv->sisusbfd, r + SISUSB_PCI_PSEUDO_IOPORTBASE - SISUSB_PCI_IOPORTBASE, SEEK_SET);\
      read(priv->sisusbfd, &b8, 1);\
      *( d ) = b8;}

     
int sisusb_setmode(KdScreenInfo *screen, struct screendata *mode, int bpp)
{
        SiSusbPriv *priv = screen->card->driver;
        int modex = mode->width;
        int modey = mode->height;
	int ret = 0, i, j, du;
    	int touchengines = 1;//0;
	unsigned char sr31, cr63, tmp8;
	static const char attrdata[] = {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		0x01,0x00,0x00,0x00
	};
	static const char crtcrdata[] = { // SiS_StandTable[3] (konstant?)
		0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,
		0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		0xea,0x8c,0xdf,0x28,0x40,0xe7,0x04,0xa3,
		0xff
	};
	static const char grcdata[] = {  // SiS_StandTable[5] (Konstant?)
		0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0f,
		0xff
	};

        char *crtcdata = mode->crtcdata;

	GETIREG(SISSR, 0x31, &sr31);
	GETIREG(SISCR, 0x63, &cr63);
	SETIREGOR(SISSR, 0x01, 0x20);
	SETIREG(SISCR, 0x63, cr63 & 0xbf);
	SETIREGOR(SISCR, 0x17, 0x80);
	SETIREGOR(SISSR, 0x1f, 0x04);
	SETIREGAND(SISSR, 0x07, 0xfb);
	SETIREG(SISSR, 0x00, 0x03);	/* seq */
	SETIREG(SISSR, 0x01, 0x21);
	SETIREG(SISSR, 0x02, 0x0f);
	SETIREG(SISSR, 0x03, 0x00);
	SETIREG(SISSR, 0x04, 0x0e);
	SETREG(SISMISCW, 0x23);		/* misc */
	for (i = 0; i <= 0x18; i++) {	/* crtc */
		SETIREG(SISCR, i, crtcrdata[i]);
	}
	for (i = 0; i <= 0x13; i++) {	/* att */
		GETREG(SISINPSTAT, &tmp8);
		SETREG(SISAR, i);
		SETREG(SISAR, attrdata[i]);
	}
	GETREG(SISINPSTAT, &tmp8);
	SETREG(SISAR, 0x14);
	SETREG(SISAR, 0x00);
	GETREG(SISINPSTAT, &tmp8);
	SETREG(SISAR, 0x20);
	GETREG(SISINPSTAT, &tmp8);
	for (i = 0; i <= 0x08; i++) {	/* grc */
		SETIREG(SISGR, i, grcdata[i]);
	}
	SETIREGAND(SISGR, 0x05, 0xbf);
	for (i = 0x0A; i <= 0x0E; i++) {	/* clr ext */
		SETIREG(SISSR, i, 0x00);
	}
	SETIREGAND(SISSR, 0x37, 0xfe);
	SETREG(SISMISCW, 0xef);		/* sync */
	SETIREG(SISCR, 0x11, 0x00);	/* crtc */
	for (j = 0x00, i = 0; i <= 7; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x10; i <= 10; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x15; i <= 12; i++, j++) {
		SETIREG(SISCR, j, crtcdata[i]);
	}
	for (j = 0x0A; i <= 15; i++, j++) {
		SETIREG(SISSR, j, crtcdata[i]);
	}
	SETIREG(SISSR, 0x0E, (crtcdata[16] & 0xE0));

	SETIREGANDOR(SISCR, 0x09, 0x5f, ((crtcdata[16] & 0x01) << 5));
	SETIREG(SISCR, 0x14, 0x4f);
	du = (modex / 16) * (bpp * 2);	/* offset/pitch */
	if (modex % 16) du += bpp;
	SETIREGANDOR(SISSR, 0x0e, 0xf0, ((du >> 8) & 0x0f));
	SETIREG(SISCR, 0x13, (du & 0xff));
	du <<= 5;
	tmp8 = du >> 8;
	if (du & 0xff) tmp8++;
	SETIREG(SISSR, 0x10, tmp8);
	SETIREG(SISSR, 0x31, 0x00);	/* VCLK */
	SETIREG(SISSR, 0x2b, mode->VCLK_a);     
	SETIREG(SISSR, 0x2c, mode->VCLK_b);     // SiSUSB_VCLKData
	SETIREG(SISSR, 0x2d, 0x01);
	SETIREGAND(SISSR, 0x3d, 0xfe);	/* FIFO */
	SETIREG(SISSR, 0x08, 0xae);
	SETIREGAND(SISSR, 0x09, 0xf0);
	SETIREG(SISSR, 0x08, 0x34);
	SETIREGOR(SISSR, 0x3d, 0x01);
	SETIREGAND(SISSR, 0x1f, 0x3f);	/* mode regs */
        if(bpp==2){
            SETIREGANDOR(SISSR, 0x06, 0xc0, 0x0a);
        }else{
            SETIREGANDOR(SISSR, 0x06, 0xc0, 0x12);  
        }      
	SETIREG(SISCR, 0x19, 0x00);
	SETIREGAND(SISCR, 0x1a, 0xfc);
	SETIREGAND(SISSR, 0x0f, 0xb7);
	SETIREGAND(SISSR, 0x31, 0xfb);
	SETIREGANDOR(SISSR, 0x21, 0x1f, 0xa0);
	SETIREGAND(SISSR, 0x32, 0xf3);
	SETIREGANDOR(SISSR, 0x07, 0xf8, 0x03);
	SETIREG(SISCR, 0x52, 0x6c);

	SETIREG(SISCR, 0x0d, 0x00);	/* adjust frame */
	SETIREG(SISCR, 0x0c, 0x00);
	SETIREG(SISSR, 0x0d, 0x00);
	SETIREGAND(SISSR, 0x37, 0xfe);

	SETIREG(SISCR, 0x32, 0x20);
	SETIREGAND(SISSR, 0x01, 0xdf);	/* enable display */
	SETIREG(SISCR, 0x63, (cr63 & 0xbf));
	SETIREG(SISSR, 0x31, (sr31 & 0xfb));

	if (touchengines) {
		SETIREG(SISSR, 0x20, 0xa1);	/* enable engines */
		SETIREGOR(SISSR, 0x1e, 0x5a);

		SETIREG(SISSR, 0x26, 0x01);	/* disable cmdqueue */
		SETIREG(SISSR, 0x27, 0x1f);
		SETIREG(SISSR, 0x26, 0x00);
	}

	return ret;
}


