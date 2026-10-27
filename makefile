# makefile for all sample code

VERSION:=0.1.0
PACKAGE_REVISION?=2
DIST:=feedkit-$(VERSION)
BACKUP_PATH:=Backup/$(shell date +%Y%m%d%H%M%S).zip
BACKUP_INCLUDE:=$(shell find | grep -E "\.cpp|\.h|\.wks|makefile|\.sh|Worksheet|\.rsrc|Icons/")

COMMON_LIB:=$(shell finddir B_COMMON_LIB_DIRECTORY)
COMMON_SERVERS:=$(shell finddir B_COMMON_SERVERS_DIRECTORY)
COMMON_ADDONS:=$(shell finddir B_COMMON_ADDONS_DIRECTORY)
BUILD:=$(shell pwd)/build
PACKAGE_ROOT?=$(BUILD)/package-root
PACKAGE_ARCH?=$(shell getarch 2>/dev/null)
PACKAGE_FILE?=$(BUILD)/feedkit-$(VERSION)-$(PACKAGE_REVISION)-$(PACKAGE_ARCH).hpkg

CORE_SUBDIRS = \
	Server \
	Parsers

OPTIONAL_SUBDIRS = \
	Preflet \
	Clients

SUBDIRS = $(CORE_SUBDIRS) Tests


FEEDKIT_HEADERS=$(addprefix /boot/home/config/include/, $(wildcard libfeedkit/*.h))

.PHONY: default clean test unit smoke integration clients client-smoke preferences preflet-smoke optional package-stage package-check package installed-package-check package-removal-prepare package-removal-check install dist common

default:
	$(MAKE) -C libfeedkit -f makefile $@ || exit 1;

	@for f in $(SUBDIRS) ; do \
		$(MAKE) -C $$f -f makefile $@ || exit 1; \
	done

optional:
	@for f in $(OPTIONAL_SUBDIRS) ; do \
		$(MAKE) -C $$f -f makefile default || exit 1; \
	done

clients: default
	$(MAKE) -C Clients -f makefile default

client-smoke: clients
	./scripts/client-smoke-test.sh

preferences: default
	$(MAKE) -C Preflet -f makefile default

preflet-smoke: preferences
	./scripts/preflet-smoke-test.sh

smoke: default
	./scripts/smoke-test.sh

integration: default
	./scripts/integration-test.sh

unit: default
	$(MAKE) -C Tests -f makefile test

test: unit smoke integration

package-stage: default
	./scripts/stage-package.sh "$(BUILD)" "$(PACKAGE_ROOT)"

package-check: package-stage
	./scripts/check-package-layout.sh "$(PACKAGE_ROOT)"

package: package-check
	./scripts/create-package.sh "$(PACKAGE_ROOT)" "$(PACKAGE_FILE)" \
		"$(VERSION)" "$(PACKAGE_REVISION)" "$(PACKAGE_ARCH)"

installed-package-check:
	$(MAKE) -C Tests/lifecycle -f makefile default
	./scripts/installed-package-test.sh

package-removal-prepare:
	./scripts/package-removal-test.sh prepare

package-removal-check:
	./scripts/package-removal-test.sh check

clean:
	$(MAKE) -C libfeedkit -f makefile clean
	@for f in $(SUBDIRS) ; do \
		$(MAKE) -C $$f -f makefile clean || exit 1; \
	done

symlinks: common
	ln -sf "$(BUILD)/libfeedkit.so" "$(COMMON_LIB)"
	ln -sf "$(BUILD)/feed_server" "$(COMMON_SERVERS)"
	ln -sf "$(BUILD)/Parsers" "$(COMMON_ADDONS)/feed_kit/Parsers"
	ln -sf "$(BUILD)/FeedKitSettings" "/boot/home/config/be/Preferences"

install: common
	copyattr --data --move "$(BUILD)/libfeedkit.so" "$(COMMON_LIB)"
	copyattr --data --move "$(BUILD)/feed_server"  "$(COMMON_SERVERS)"
	copyattr --data --recursive --move "$(BUILD)/Parsers" "$(COMMON_ADDONS)/Feed_Kit/Parsers"

common:
	ln -sf "/boot/home/config/lib/libfeedkit.so" "/boot/develop/lib/x86/libfeedkit.so"

	-if [ ! -d "$(COMMON_SERVERS)" ]; then \
		mkdir -p "$(COMMON_SERVERS)"; \
	fi 

	rm -Rf "$(COMMON_ADDONS)/feed_kit"
	mkdir -p "$(COMMON_ADDONS)/feed_kit"


dist:
	mkdir -p "$(DIST)"
	copyattr --data --recursive build/* "$(DIST)"
	mkdir -p "$(DIST)/doc"
	copyattr --data --recursive build/* "$(DIST)/doc"
	zip -r "$(DIST).zip" "$(DIST)/"
	rm -Rf "$(DIST)"
	
backup:
	zip -r $(BACKUP_PATH) . -i $(BACKUP_INCLUDE)
	
/boot/home/config/include/libfeedkit:
	-mkdir -p $@

/boot/home/config/include/libfeedkit/%.h: libfeedkit/%.h
	cp -f $< $@	
