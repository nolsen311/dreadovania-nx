# SPDX-License-Identifier: GPL-3.0-or-later
# Builds dreadovania.nro with devkitA64 and libnx (ADR 0004). Adapted from
# devkitPro's libnx application template. Run it in the pinned devkitPro
# container: see README.md, "Building".
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

VERSION_STRING := $(strip $(file < $(TOPDIR)/VERSION))

TARGET      := dreadovania
BUILD       := build
SOURCES     := source source/ui core/src
DATA        :=
INCLUDES    := core/include source
ICON        := icon.jpg
APP_TITLE   := Dreadovania
APP_AUTHOR  := Dreadovania contributors
APP_VERSION := $(VERSION_STRING)

#---------------------------------------------------------------------------------
# code generation
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES  := -DDREADOVANIA_VERSION=\"$(VERSION_STRING)\"
CFLAGS   := -g -Wall -Wextra -Werror -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcurl -lSDL2_ttf -lharfbuzz -lfreetype -lbz2 -lpng16 -lSDL2_gfx -lSDL2 \
            -lEGL -lglapi -ldrm_nouveau -lz -lnx
# Our own libcurl build comes first (scripts/build-curl.sh, ADR 0015).
CURL_PREFIX := $(TOPDIR)/build-curl/prefix
LIBDIRS  := $(CURL_PREFIX) $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export LD          := $(CXX)
export OFILES_SRC  := $(CPPFILES:.cpp=.o)
export OFILES      := $(OFILES_SRC)
export INCLUDE     := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                      $(foreach dir,$(LIBDIRS),-isystem $(dir)/include) -isystem $(PORTLIBS)/include/SDL2 \
                      -I$(CURDIR)/$(BUILD)
export LIBPATHS    := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON    := $(TOPDIR)/$(ICON)
export NROFLAGS    += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(CURL_PREFIX)/lib/libcurl.a: scripts/build-curl.sh patches/curl/dreadovania-libnx.patch
	@scripts/build-curl.sh

$(BUILD): $(CURL_PREFIX)/lib/libcurl.a
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
	@echo "(build-curl/ is kept; delete it to rebuild libcurl)"

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
