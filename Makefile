TARGET   = MopiTube
OBJS     = src/main.o src/config.o src/net.o src/mpd.o src/input.o src/ui.o src/artwork.o src/http.o

CFLAGS   = -O2 -G0 -Wall -Wextra -std=c99
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  = $(CFLAGS)

# Order matters: dependent libs must precede their dependencies so the
# stub sections in the final ELF are laid out in the order psp-fixup-imports
# expects. pspnet_resolver -> pspnet_inet -> pspnet_apctl -> pspnet.
# build.mak appends pspdebug/pspdisplay/pspge/pspctrl/pspnet/pspnet_apctl after
# this list; we deliberately re-list pspnet_apctl earlier so its stub appears
# before pspnet's.
LIBS = \
	-lintrafont -ljpeg -lpng -lz \
	-lpspgu -lpspgum \
	-lpspnet_resolver -lpspnet_inet -lpspnet_apctl \
	-lpsputility \
	-lpsppower \
	-lpsprtc \
	-lm

EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = MopiTube
PSP_EBOOT_ICON  = ICON0.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
