CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
OUT ?= build

.PHONY: all test clean
all:
	cmake -S . -B "$(OUT)/unified" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	cmake --build "$(OUT)/unified" --target exfat-unified --parallel 2
	mkdir -p "$(OUT)"
	cp "$(OUT)/unified/exFAT.so" "$(OUT)/exFAT.so"

$(OUT)/exfat-realimage-test: tests/exfat_so_realimage_test.c include/infiltratr/filesystem_module_v2.h
	mkdir -p "$(OUT)"
	$(CC) $(CFLAGS) -Iinclude $< -ldl -o "$@"

test: all $(OUT)/exfat-realimage-test
	cmake --build "$(OUT)/unified" --target exfat-unified-contract --parallel 2
	ctest --test-dir "$(OUT)/unified" --output-on-failure
	nm -D --defined-only "$(OUT)/exFAT.so" | grep -F 'infiltrator_filesystem_api_v2'
	! nm -D --defined-only "$(OUT)/exFAT.so" | grep -E 'infiltrator_filesystem_.*_v1|infiltrator_filesystem_module_v[345]'
	@set -eu; tmp=$$(mktemp -d /tmp/ifs-exfat-XXXXXXXX); trap 'rm -rf -- "$$tmp"' EXIT; \
	  truncate -s 128M "$$tmp/exfat.img"; truncate -s 128M "$$tmp/fat32.img"; \
	  mkfs.exfat -n SOQUAL "$$tmp/exfat.img"; mkfs.fat -F 32 -I "$$tmp/fat32.img"; \
	  "$(OUT)/exfat-realimage-test" "$(OUT)/exFAT.so" "$$tmp/exfat.img" "$$tmp/fat32.img"; \
	  fsck.exfat -n "$$tmp/exfat.img"
	@echo "Full strict Common V2 exFAT.so real-media qualification passed."

clean:
	rm -rf -- "$(OUT)"
