CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
SRC := native/filesystems
PORT := $(SRC)/fat/portable
EX := $(SRC)/exfat/module
OUT ?= build
SOURCES := $(EX)/exfat_module.c $(PORT)/ff.c $(PORT)/ffunicode.c $(PORT)/fatfs_libc.c
FLAGS := -DIFS_FATFS_ONLY=64 -I$(EX) -I$(PORT)
FREESTANDING_LINK ?= -nostdlib
STRICT_SHARED_LINK ?= -Wl,-z,defs

.PHONY: all legacy test clean
# Only the unified driver is distributable; the freestanding V3/V4/V5-only
# reader is preserved as a separate ABI test artifact.
all:
	cmake -S . -B "$(OUT)/unified" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	cmake --build "$(OUT)/unified" --target exfat-unified --parallel 2
	mkdir -p "$(OUT)"
	cp "$(OUT)/unified/exFAT.so" "$(OUT)/exFAT.so"

$(OUT):
	mkdir -p "$(OUT)"
legacy: $(OUT)/exFAT-legacy.so
$(OUT)/exFAT-legacy.so: $(SOURCES) | $(OUT)
	$(CC) $(CFLAGS) $(FLAGS) -fPIC -fno-builtin -fno-stack-protector -ffreestanding -fvisibility=hidden $(FREESTANDING_LINK) -shared $(STRICT_SHARED_LINK) -Wl,-z,relro -Wl,-z,now $^ -o "$@"

$(OUT)/exfat-realimage-test: tests/exfat_so_realimage_test.c $(EX)/exfat_api.h $(PORT)/fatfs_module.h | $(OUT)
	$(CC) $(CFLAGS) -I$(EX) -I$(PORT) $< -ldl -o "$@"

test: legacy $(OUT)/exfat-realimage-test
	readelf -h $(OUT)/exFAT-legacy.so | grep -Eq 'Type:.*DYN'
	for version in 3 4 5; do readelf -Ws $(OUT)/exFAT-legacy.so | grep -F "infiltrator_filesystem_module_v$$version"; done
	for version in 3 4 5; do readelf -p .ifsmeta $(OUT)/exFAT-legacy.so | grep -F "IFS-MODULE:exfat:$$version"; done
	! readelf -d $(OUT)/exFAT-legacy.so | grep -q '[(]NEEDED[)]'
	$(MAKE) all OUT="$(OUT)"
	cmake --build "$(OUT)/unified" --target exfat-unified-contract --parallel 2
	ctest --test-dir "$(OUT)/unified" --output-on-failure
	for version in 3 4 5; do nm -D "$(OUT)/exFAT.so" | grep -F "infiltrator_filesystem_module_v$$version"; done
	nm -D "$(OUT)/exFAT.so" | grep -F 'infiltrator_filesystem_exfat_defrag_v1'
	@set -eu; tmp=$$(mktemp -d /tmp/ifs-exfat-XXXXXXXX); trap 'rm -rf -- "$$tmp"' EXIT; \
	  truncate -s 128M "$$tmp/exfat.img"; truncate -s 128M "$$tmp/fat32.img"; \
	  mkfs.exfat -n SOQUAL "$$tmp/exfat.img"; mkfs.fat -F 32 -I "$$tmp/fat32.img"; \
	  "$(OUT)/exfat-realimage-test" "$(OUT)/exFAT-legacy.so" "$$tmp/exfat.img" "$$tmp/fat32.img"; \
	  fsck.exfat -n "$$tmp/exfat.img"; \
	  mkfs.exfat -n UNIFIED "$$tmp/exfat.img"; \
	  "$(OUT)/exfat-realimage-test" "$(OUT)/exFAT.so" "$$tmp/exfat.img" "$$tmp/fat32.img"; \
	  fsck.exfat -n "$$tmp/exfat.img"
	@echo "Full exFAT.so V3/V4/V5 + Defrag ABI and independent real-media qualification passed."

clean:
	rm -rf -- "$(OUT)"
