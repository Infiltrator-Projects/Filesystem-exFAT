CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
SRC := native/filesystems
PORT := $(SRC)/fat/portable
EX := $(SRC)/exfat/module
OUT ?= build
SOURCES := $(EX)/exfat_module.c $(PORT)/ff.c $(PORT)/ffunicode.c $(PORT)/fatfs_libc.c
FLAGS := -DIFS_FATFS_ONLY=64 -I$(EX) -I$(PORT)
# Training uses the normal host runtime for gcov; release stays freestanding.
FREESTANDING_LINK ?= -nostdlib
.PHONY: all test clean
all: $(OUT)/exFAT.so
$(OUT):
	mkdir -p "$(OUT)"

$(OUT)/exFAT.so: $(SOURCES) | $(OUT)
	$(CC) $(CFLAGS) $(FLAGS) -fPIC -fno-builtin -fno-stack-protector -ffreestanding -fvisibility=hidden $(FREESTANDING_LINK) -shared -Wl,-z,defs -Wl,-z,relro -Wl,-z,now $^ -o "$@"

$(OUT)/exfat-realimage-test: tests/exfat_so_realimage_test.c $(EX)/exfat_api.h $(PORT)/fatfs_module.h | $(OUT)
	$(CC) $(CFLAGS) -I$(EX) -I$(PORT) $< -ldl -o "$@"

test: $(OUT)/exFAT.so $(OUT)/exfat-realimage-test
	readelf -h $(OUT)/exFAT.so | grep -Eq 'Type:.*DYN'
	for version in 3 4 5; do readelf -Ws $(OUT)/exFAT.so | grep -F "infiltrator_filesystem_module_v$$version"; done
	for version in 3 4 5; do readelf -p .ifsmeta $(OUT)/exFAT.so | grep -F "IFS-MODULE:exfat:$$version"; done
	! readelf -d $(OUT)/exFAT.so | grep -q '[(]NEEDED[)]'
	@set -eu; tmp=$$(mktemp -d /tmp/ifs-exfat-XXXXXXXX); trap 'rm -rf -- "$$tmp"' EXIT; \
	  truncate -s 128M "$$tmp/exfat.img"; truncate -s 128M "$$tmp/fat32.img"; \
	  mkfs.exfat -n SOQUAL "$$tmp/exfat.img"; mkfs.fat -F 32 -I "$$tmp/fat32.img"; \
	  "$(OUT)/exfat-realimage-test" "$(OUT)/exFAT.so" "$$tmp/exfat.img" "$$tmp/fat32.img"; \
	  fsck.exfat -n "$$tmp/exfat.img"
	@echo "Independent exFAT read/write/format media qualification passed."

clean:
	rm -rf -- "$(OUT)"
