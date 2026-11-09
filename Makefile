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
SOURCES     := source core/src
DATA        :=
INCLUDES    := core/include
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
LIBS     := -lnx
LIBDIRS  := $(PORTLIBS) $(LIBNX)

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
                      $(foreach dir,$(LIBDIRS),-isystem $(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS    := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON    := $(TOPDIR)/$(ICON)
export NROFLAGS    += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

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
