# PKG Manager X - PS4 home screen tile (fake PKG), built with the OpenOrbis
# PS4 toolchain (https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).
#
#   make OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain PAYLOAD=../pkgmgr-ps4.elf
#
# Output: $(CONTENT_ID).pkg (install it with GoldHEN's Package Installer).
# The tile starts the bundled payload through GoldHEN's BinLoader when PKG
# Manager X is not running, then opens http://127.0.0.1:8844/ in the browser.

TITLE       := PKG Manager X
TITLE_ID    := PKGX00001
CONTENT_ID  := IV0000-PKGX00001_00-PKGMANAGERX00000
# Package version (xx.yy). Bump it when the bundled payload changes so the
# package installs over the previous one.
VERSION     ?= 01.00
PAYLOAD     ?= ../pkgmgr-ps4.elf
ICON        ?= ../assets/icon0.png

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
BUILD       := build
CC          ?= clang
LD          ?= ld.lld
TOOLS       := $(TOOLCHAIN)/bin/linux

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -Wall \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib -lc -lkernel -lSceUserService -lSceSystemService \
               $(TOOLCHAIN)/lib/crt1.o

STAGE_FILES := eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx pkgmgr-ps4.elf

all: $(CONTENT_ID).pkg

$(BUILD)/main.o: main.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/launcher.elf: $(BUILD)/main.o
	$(LD) $< -o $@ $(LDFLAGS)

eboot.bin: $(BUILD)/launcher.elf
	$(TOOLS)/create-fself -in=$< -out=$(BUILD)/launcher.oelf --eboot "eboot.bin" --paid 0x3800000000000011

sce_sys/param.sfo: Makefile
	@mkdir -p sce_sys
	rm -f $@
	$(TOOLS)/PkgTool.Core sfo_new $@
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

# The PS4 home screen expects a 512x512 icon0.png.
sce_sys/icon0.png: $(ICON)
	@mkdir -p sce_sys
	python3 -c "from PIL import Image; Image.open('$<').convert('RGB').resize((512, 512), Image.LANCZOS).save('$@')"

sce_sys/about/right.sprx:
	@mkdir -p sce_sys/about
	cp "$$(find $(TOOLCHAIN)/samples -path '*sce_sys/about/right.sprx' | head -1)" $@

pkgmgr-ps4.elf: $(PAYLOAD)
	cp $< $@

pkg.gp4: $(STAGE_FILES)
	$(TOOLS)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$(STAGE_FILES)"

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLS)/PkgTool.Core pkg_build $< .

clean:
	rm -rf $(BUILD) eboot.bin sce_sys pkg.gp4 pkgmgr-ps4.elf $(CONTENT_ID).pkg

.PHONY: all clean
