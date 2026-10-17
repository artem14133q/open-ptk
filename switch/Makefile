#---------------------------------------------------------------------------------
# The Nintendo Switch build: petka.nro, the launcher and the engine's core on
# raylib-nx. Needs devkitPro with libnx, switch-mesa and switch-libdrm_nouveau,
# and raylib-nx built by ./raylib.sh first (into deps/). See README.md.
#
# After libnx's application template (examples/switch/templates/application).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET		:=	petka
BUILD		:=	build
SOURCES		:=	../core/src ../launcher/src
DATA		:=
INCLUDES	:=	../core/include deps/raylib-nx/src deps/ffmpeg/include

APP_TITLE	:=	Петька и Василий Иванович спасают галактику
APP_AUTHOR	:=	Бука & S.K.I.F. & artem14133q
APP_VERSION	:=	0.1.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -std=gnu11 \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DPLATFORM_NX

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) \
			-L$(TOPDIR)/deps/raylib-nx/src -L$(TOPDIR)/deps/ffmpeg/lib

# The films: FFmpeg as switch/ffmpeg.sh builds it.
LIBS	:= -lavformat -lavcodec -lswscale -lswresample -lavutil \
			-lraylib -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx -lm

LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# (the rest is the template's)
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

# Mesa, under EGL and GLES, is C++: link with the C++ driver.
export LD	:=	$(CXX)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all nsp nsp-full

all: $(BUILD)

# petka.nsp, the game as an installed title (nsp.sh: needs hacBrewPack and
# the console's keys).
nsp: all
	@APP_TITLE="$(APP_TITLE)" APP_AUTHOR="$(APP_AUTHOR)" APP_VERSION="$(APP_VERSION)" ./nsp.sh

# petka-full.nsp, the same with the game's files inside (assets.txt, taken
# from the folder PETKA_ASSETS names).
nsp-full: all
	@APP_TITLE="$(APP_TITLE)" APP_AUTHOR="$(APP_AUTHOR)" APP_VERSION="$(APP_VERSION)" ./nsp.sh full

$(BUILD):
	@[ -f deps/raylib-nx/src/libraylib.a ] || { echo "raylib-nx is not built: run ./raylib.sh first"; exit 1; }
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(TARGET).nsp $(TARGET)-full.nsp

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

-include $(DEPENDS)

endif
