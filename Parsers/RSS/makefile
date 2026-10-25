# FeedKit RSS parser build

TOPDIR := ../..
NAME := RSS
TARGET_DIR := $(TOPDIR)/build/Parsers
TYPE := SHARED
SRCS := RSSParser.cpp

LIBXML_AVAILABLE := $(shell pkg-config --exists libxml-2.0 2>/dev/null && echo yes)
LIBXML_INCLUDE_PATHS := $(patsubst -I%,%,$(shell pkg-config --cflags-only-I libxml-2.0 2>/dev/null))

ifeq ($(LIBXML_AVAILABLE),)

.PHONY: default clean

default:
	@echo "The RSS parser requires libxml2_devel. Install it with: pkgman install libxml2_devel" >&2
	@exit 1

clean:
	@rm -rf dep/* obj.x86/* obj.ppc/*

else

LIBS := be feedkit xml2 z iconv stdc++
LIBPATHS := /boot/home/config/lib
SYSTEM_INCLUDE_PATHS := $(LIBXML_INCLUDE_PATHS)
LOCAL_INCLUDE_PATHS :=
OPTIMIZE := SOME
WARNINGS := ALL
SYMBOLS := TRUE
INSTALL_DIR := $(shell finddir B_COMMON_ADDONS_DIRECTORY)/feed_kit/Parsers

include $(TOPDIR)/makefile-engine.FeedKit

endif
