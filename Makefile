# Nemo Launcher — host tests, PSP build (Docker), install to memory stick.

CORE_SRC   := $(wildcard src/core/*.c) third_party/cjson/cJSON.c
TEST_SRC   := $(wildcard tests/*.c)
TEST_BIN   := build/run_tests
HOST_CFLAGS := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -O1 \
               -fsanitize=address,undefined -fno-omit-frame-pointer \
               -Isrc/core -Ithird_party/cjson

PSP_IMAGE  := pspdev/pspdev:latest
CARD_DIR   := /media/schastlivyy/disk/PSP/SAVEDATA/ARK_01234
# ARK-5 distribution in the project (same LANG.ARK as on the console)
ARK_LANG   := ../FasterARK_psp_full/PSP/SAVEDATA/ARK_01234/LANG.ARK

.PHONY: test fixtures psp xmblang install release clean

test: fixtures $(TEST_BIN)
	./$(TEST_BIN) $(T)

fixtures:
	@mkdir -p build
	python3 tests/fixtures/make_fixtures.py build/fixtures

$(TEST_BIN): $(CORE_SRC) $(TEST_SRC) $(wildcard src/core/*.h) tests/test.h
	@mkdir -p build
	cc $(HOST_CFLAGS) -o $@ $(CORE_SRC) $(TEST_SRC) -lz

# XMB item name "Nemo Launcher" (lang_ru.json / lang_en.json next to VBOOT.PBP)
xmblang:
	@mkdir -p dist
	python3 tools/make_xmb_lang.py "$(ARK_LANG)" dist

psp: xmblang
	@mkdir -p build/psp/src/core build/psp/src/psp build/psp/third_party/cjson build/psp/third_party/libpspftp/src build/psp/gen dist
	python3 tools/bin2c.py assets/fonts/Inter-Regular.ttf build/psp/gen/font_regular.c font_regular
	python3 tools/bin2c.py assets/fonts/Inter-SemiBold.ttf build/psp/gen/font_semibold.c font_semibold
	python3 tools/bin2c.py assets/ncl_logo.png build/psp/gen/ncl_logo.c ncl_logo
	python3 tools/bin2c.py assets/nemo_logo.png build/psp/gen/nemo_logo.c nemo_logo
	python3 tools/bin2c.py assets/testers.txt build/psp/gen/testers_txt.c testers_txt
	python3 tools/credits_txt.py docs/CREDITS.md build/psp/gen/credits_ru.txt
	python3 tools/credits_txt.py docs/CREDITS.en.md build/psp/gen/credits_en.txt
	python3 tools/bin2c.py build/psp/gen/credits_ru.txt build/psp/gen/credits_ru.c credits_ru
	python3 tools/bin2c.py build/psp/gen/credits_en.txt build/psp/gen/credits_en.c credits_en
	docker run --rm -u $$(id -u):$$(id -g) -v "$(CURDIR)":/src -w /src $(PSP_IMAGE) \
		make -C build/psp -f ../../Makefile.psp
	python3 tools/check_stubs.py build/psp/nemo.elf
	cp build/psp/EBOOT.PBP dist/VBOOT.PBP
	@ls -l dist/VBOOT.PBP

install:
	@test -d "$(CARD_DIR)" || { echo "Карта PSP не подключена: нет $(CARD_DIR)"; exit 1; }
	@test -f "$(CARD_DIR)/VBOOT_ARK.PBP" || { echo "Нет $(CARD_DIR)/VBOOT_ARK.PBP — резервная копия лаунчера ARK. Установка отменена."; exit 1; }
	@test -f dist/VBOOT.PBP || { echo "Нет dist/VBOOT.PBP — сначала make psp"; exit 1; }
	cp dist/VBOOT.PBP "$(CARD_DIR)/VBOOT.PBP"
	cp dist/lang_ru.json dist/lang_en.json "$(CARD_DIR)/"
	sync
	md5sum dist/VBOOT.PBP "$(CARD_DIR)/VBOOT.PBP" "$(CARD_DIR)/VBOOT_ARK.PBP"

# GitHub release archives (1.1.12): CHANNEL = PROD | PRE-ALPHA | DEV
#   lite_no_ark-5  : PSP/SAVEDATA/ARK_01234/{VBOOT.PBP, lang_ru.json, lang_en.json} — ARK-5 already installed
#   full_with_ark-5: ARK-5 (PSP/ of FasterARK_psp_full.zip, its fan themes left out) + Nemo; ARK's own
#                    launcher kept as VBOOT_ARK.PBP
VERSION    := $(shell sed -n 's/^\#define NEMO_VERSION "\(.*\)"/\1/p' src/psp/version.h)
CHANNEL    ?= DEV
# ARK-5 "full" package of FasterARK (PSP-Arkfive); override: make release ARK_FULL=/path/to.zip
ARK_FULL   ?= ../../FasterARK_psp_full.zip
REL        := build/release
REL_NAME   := Nemo_Launcher_$(VERSION)_$(CHANNEL)
ARK_DIR    := PSP/SAVEDATA/ARK_01234

release: psp   # run "make test" locally first (tests are not published)
	rm -rf $(REL) && mkdir -p $(REL)/lite/$(ARK_DIR) $(REL)/full dist/release
	cp dist/VBOOT.PBP dist/lang_ru.json dist/lang_en.json $(REL)/lite/$(ARK_DIR)/
	cp README.md README.ru.md LICENSE $(REL)/lite/
	cd $(REL)/lite && zip -qr "$(CURDIR)/dist/release/$(REL_NAME)_lite_no_ark-5.zip" .
	unzip -q "$(ARK_FULL)" "PSP/*" -d $(REL)/full
	mv $(REL)/full/$(ARK_DIR)/VBOOT.PBP $(REL)/full/$(ARK_DIR)/VBOOT_ARK.PBP
	cp dist/VBOOT.PBP dist/lang_ru.json dist/lang_en.json $(REL)/full/$(ARK_DIR)/
	cp README.md README.ru.md LICENSE $(REL)/full/
	cd $(REL)/full && zip -qr "$(CURDIR)/dist/release/$(REL_NAME)_full_with_ark-5.zip" .
	@ls -l dist/release/

clean:
	rm -rf build dist
