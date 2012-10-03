#======================================================================
# ezQuake Makefile
# based on: Fuhquake Makefile && ZQuake Makefile && JoeQuake Makefile
#======================================================================

# compilation tool and detection of targets/achitecture
_E = @
CC = gcc
CC_BASEVERSION = $(shell $(CC) -dumpversion | sed -e 's/\..*//g')

# TYPE = release debug
TYPE=release
STRIP = $(_E)strip
STRIPFLAGS = --strip-unneeded --remove-section=.comment

# ARCH = x86 ppc
# OS = linux darwin freebsd
ARCH = $(shell uname -m | sed -e 's/i.86/x86/g' -e 's/Power Macintosh/ppc/g' -e 's/amd64/x86_64/g')
OS = $(shell uname -s | tr A-Z a-z)

# add special architecture based flags
ifeq ($(ARCH),x86_64)
	ARCH_CFLAGS = -march=native -m64
endif
ifeq ($(ARCH),x86)
	ARCH_CFLAGS = -march=i686 -mtune=generic -mmmx -Did386
endif
ifeq ($(ARCH),ppc)
	ARCH_CFLAGS = -arch ppc -faltivec -maltivec -mcpu=7450 -mtune=7450 -mpowerpc -mpowerpc-gfxopt
endif

ifeq ($(OS),linux)
	DEFAULT_TARGET = glx
	OS_GL_CFLAGS = -DWITH_VMODE -DWITH_PULSEAUDIO
endif
ifeq ($(OS),darwin)
	ARCH_CFLAGS = -arch i686 -arch ppc -msse2
	STRIPFLAGS =
	DEFAULT_TARGET = mac
	OS_GL_CFLAGS = -I/opt/local/include/ -I/Developer/Headers/FlatCarbon -FOpenGL -FAGL
endif
ifeq ($(OS),freebsd)
	DEFAULT_TARGET = glx
	OS_GL_CFLAGS = -DWITH_DGA -DWITH_VMODE -DWITH_KEYMAP -DWITH_PULSEAUDIO
endif

LIB_PREFIX=$(OS)-$(ARCH)

default_target: $(DEFAULT_TARGET)

all: glx x11

################################
# Directories for object files #
################################

GLX_DIR	= $(TYPE)-$(ARCH)/glx
X11_DIR	= $(TYPE)-$(ARCH)/x11
MAC_DIR	= $(TYPE)-$(ARCH)/mac

################
# Binary files #
################

GLX_TARGET = $(TYPE)-$(ARCH)/ezquake-gl.glx
X11_TARGET = $(TYPE)-$(ARCH)/ezquake.x11
MAC_TARGET = $(TYPE)-$(ARCH)/ezquake-gl.mac
QUAKE_DIR="/opt/quake/"

################

C_BUILD = $(_E)$(CC) -MD -c -o $@ $(_CFLAGS) $<
S_BUILD = $(_E)$(CC) -c -o $@ $(_CFLAGS) -DELF -x assembler-with-cpp $<
BUILD = $(_E)$(CC) -o $@ $(_OBJS) $(_LDFLAGS)
MKDIR = $(_E)mkdir -p $@

################

$(GLX_DIR) $(X11_DIR) $(MAC_DIR):
	$(MKDIR)

# compiler flags
# -DWITH_XMMS      for xmms      MP3 player support
# -DWITH_AUDACIOUS for audacious MP3 player support
# -DWITH_XMMS2     for xmms2     MP3 player support
# -DWITH_MPD       for mpd       MP3 player support
# -DWITH_WINAMP    for winamp    MP3 player support
PRJ_CFLAGS = -DWITH_ZLIB -DWITH_PNG -DJSS_CAM -DWITH_ZIP -DUSE_PR2 -DWITH_IRC -DWITH_TCL -DWITH_NQPROGS
BASE_CFLAGS = -pipe -Wall -funsigned-char $(ARCH_CFLAGS) $(PRJ_CFLAGS) -I./libs -I./libs/libircclient

########################
# pkg-config includes  #
########################
# Add support for MP3 Libraries
ifneq ($(shell echo $(PRJ_CFLAGS) | grep WITH_XMMS),)
BASE_CFLAGS += `pkg-config --libs-only-L --libs --cflags glib-2.0`
endif # WITH_XMMS
ifneq ($(shell echo $(PRJ_CFLAGS) |grep WITH_AUDACIOUS),)
BASE_CFLAGS += `pkg-config --libs-only-L --libs --cflags glib-2.0 dbus-glib-1`
endif # WITH_AUDACIOUS
ifneq ($(shell echo $(PRJ_CFLAGS) | grep WITH_XMMS2),)
BASE_CFLAGS += `pkg-config --libs-only-L --libs --cflags xmms2-client`
endif # WITH_XMMS2

ifneq ($(shell echo $(PRJ_CFLAGS) | grep WITH_OGG_VORBIS),)
BASE_CFLAGS += `pkg-config --libs-only-L --libs --cflags vorbisfile`
endif # WITH_OGG_VORBIS

########################

RELEASE_CFLAGS = -O2 -fno-strict-aliasing
DEBUG_CFLAGS = -ggdb

# opengl builds
GLCFLAGS=-DGLQUAKE -DWITH_JPEG $(OS_GL_CFLAGS)

ifeq ($(TYPE),release)
CFLAGS = $(BASE_CFLAGS) $(RELEASE_CFLAGS) -DNDEBUG
LDFLAGS = -lm -lpthread -lrt
else
CFLAGS = $(BASE_CFLAGS) $(DEBUG_CFLAGS) -D_DEBUG
LDFLAGS = -ggdb -lm -lpthread -lrt
endif

COMMON_LIBS = libs/$(LIB_PREFIX)/minizip.a libs/$(LIB_PREFIX)/libpng.a libs/$(LIB_PREFIX)/libz.a libs/$(LIB_PREFIX)/libpcre.a libs/$(LIB_PREFIX)/libexpat.a libs/$(LIB_PREFIX)/libtcl.a libs/$(LIB_PREFIX)/libircclient.a libs/$(LIB_PREFIX)/libcurl.a
GL_LIBS = libs/$(LIB_PREFIX)/libjpeg.a

ifeq ($(OS),freebsd)
LOCALBASE ?= /usr/local
CFLAGS += -I$(LOCALBASE)/include
LDFLAGS += -L$(LOCALBASE)/lib
endif

ifeq ($(OS),linux)
LDFLAGS += -lX11 -ldl
endif

include Makefile.list

#######
# GLX #
#######

GLX_C_OBJS = $(addprefix $(GLX_DIR)/, $(addsuffix .o, $(GLX_C_FILES)))
GLX_S_OBJS = $(addprefix $(GLX_DIR)/, $(addsuffix .o, $(GLX_S_FILES)))
GLX_CFLAGS = $(CFLAGS) $(GLCFLAGS)
GLX_LDFLAGS = $(LDFLAGS) -lGL -lXxf86vm -lXpm -lXi

glx: _DIR = $(GLX_DIR)
glx: _OBJS = $(GLX_C_OBJS) $(GLX_S_OBJS) $(COMMON_LIBS) $(GL_LIBS)
glx: _LDFLAGS = $(GLX_LDFLAGS)
glx: _CFLAGS = $(GLX_CFLAGS)
glx: $(GLX_TARGET)

$(GLX_TARGET): $(GLX_DIR) $(GLX_C_OBJS) $(GLX_S_OBJS)
	@echo [LINK] $@
	$(BUILD)
ifeq ($(TYPE),release)
	@echo [STRIP] $@
	$(STRIP) $(STRIPFLAGS) $(GLX_TARGET)
endif

df_glx = $(GLX_DIR)/$(*F)

$(GLX_C_OBJS): $(GLX_DIR)/%.o: %.c
	@echo [CC] $<
	$(C_BUILD); \
		cp $(df_glx).d $(df_glx).P; \
		sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
			-e '/^$$/ d' -e 's/$$/ :/' < $(df_glx).d >> $(df_glx).P; \
		rm -f $(df_glx).d

$(GLX_S_OBJS): $(GLX_DIR)/%.o: %.s
	@echo [CC] $<
	$(S_BUILD)

-include $(GLX_C_OBJS:.o=.P)

#######
# X11 #
#######

X11_C_OBJS = $(addprefix $(X11_DIR)/, $(addsuffix .o, $(X11_C_FILES)))
X11_S_OBJS = $(addprefix $(X11_DIR)/, $(addsuffix .o, $(X11_S_FILES)))
X11_CFLAGS = $(CFLAGS) -D_Soft_X11
X11_LDFLAGS = $(LDFLAGS) -lX11 -lXext -lXpm

x11: _DIR = $(X11_DIR)
x11: _OBJS = $(X11_C_OBJS) $(X11_S_OBJS) $(COMMON_LIBS)
x11: _LDFLAGS = $(X11_LDFLAGS)
x11: _CFLAGS = $(X11_CFLAGS)
x11: $(X11_TARGET)

$(X11_TARGET): $(X11_DIR) $(X11_C_OBJS) $(X11_S_OBJS)
	@echo [LINK] $@
	$(BUILD)
ifeq ($(TYPE),release)
	@echo [STRIP] $@
	$(STRIP) $(STRIPFLAGS) $(X11_TARGET)
endif

df_x11 = $(X11_DIR)/$(*F)

$(X11_C_OBJS): $(X11_DIR)/%.o: %.c
	@echo [CC] $<
	$(C_BUILD); \
		cp $(df_x11).d $(df_x11).P; \
		sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
			-e '/^$$/ d' -e 's/$$/ :/' < $(df_x11).d >> $(df_x11).P; \
		rm -f $(df_x11).d

$(X11_S_OBJS): $(X11_DIR)/%.o: %.s
	@echo [CC] $<
	$(S_BUILD)

-include $(X11_C_OBJS:.o=.P)

#######
# MAC #
#######

MAC_C_OBJS = $(addprefix $(MAC_DIR)/, $(addsuffix .o, $(MAC_C_FILES)))
MAC_CFLAGS = $(CFLAGS) $(GLCFLAGS)
MAC_LDFLAGS = $(LDFLAGS) -arch i686 -arch ppc -isysroot /Developer/SDKs/MacOSX10.5.sdk -framework OpenGL -framework AGL -framework DrawSprocket -framework Carbon -framework ApplicationServices -framework IOKit

mac: _DIR = $(MAC_DIR)
mac: _OBJS = $(MAC_C_OBJS) $(COMMON_LIBS) $(GL_LIBS)
mac: _LDFLAGS = $(MAC_LDFLAGS)
mac: _CFLAGS = $(MAC_CFLAGS)
mac: $(MAC_TARGET)

$(MAC_TARGET): $(MAC_DIR) $(MAC_C_OBJS) $(MAC_S_OBJS)
	@echo [LINK] $@
	$(BUILD)
ifeq ($(TYPE),release)
	@echo [STRIP] $@
	$(STRIP) $(STRIPFLAGS) $(MAC_TARGET)
endif

$(MAC_C_OBJS): $(MAC_DIR)/%.o: %.c
	@echo [CC] $<
	$(C_BUILD)

#################
clean:
	@echo [CLEAN]
	@-rm -rf $(GLX_DIR) $(X11_DIR) $(MAC_DIR)

help:
	@echo "all     - make all the targets possible"
	@echo "install - Installs all made clients to /opt/quake"
	@echo "clean   - removes all output"
	@echo "glx     - GLX GL client"
	@echo "x11     - X11 software client"
	@echo "mac     - Mac client"


install:
	@echo [CP] $(GLX_TARGET) 	$(QUAKE_DIR)
	@cp $(GLX_TARGET) 			$(QUAKE_DIR)
	@echo [CP] $(X11_TARGET) 	$(QUAKE_DIR)
	@cp $(X11_TARGET)			$(QUAKE_DIR)
#	@echo [CP] $(MAC_TARGET) 	$(QUAKE_DIR)
#	@cp $(MAC_TARGET)			$(QUAKE_DIR)
