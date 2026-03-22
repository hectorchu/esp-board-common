.PHONY: docker-build docker-shell dist clean

BOARD ?= waveshare_s3_lcd35b

# Reuse the prebaked ESP-IDF v5.5.1 + ESP32-S3 toolchain image from
# seedsigner-micropython-builder.
IMAGE ?= ghcr.io/kdmukai-bot/seedsigner-micropython-builder-base:latest

# Mount the entire esp-board-common repo so the EXTRA_COMPONENT_DIRS path resolves.
# The working directory is the app directory inside the container.
# A named volume persists the ccache across builds.
DOCKER_RUN = docker run --rm \
	--user $(shell id -u):$(shell id -g) \
	-e HOME=/tmp/home \
	-e CCACHE_DIR=/workspace/.ccache \
	-v $(realpath ../..):/workspace/esp-board-common \
	-v $(HOME)/.cache/esp-board-common-ccache:/workspace/.ccache \
	--tmpfs /tmp/home:uid=$(shell id -u),gid=$(shell id -g) \
	-w /workspace/esp-board-common/apps/camera_viewfinder

IDF_CMD = source /opt/toolchains/esp-idf/export.sh >/dev/null 2>&1

DIST_DIR = dist/$(BOARD)

docker-build:
	$(DOCKER_RUN) -t $(IMAGE) bash -lc '$(IDF_CMD) && idf.py --ccache -DBOARD=$(BOARD) build'

dist: docker-build
	rm -rf $(DIST_DIR)
	mkdir -p $(DIST_DIR)/bootloader $(DIST_DIR)/partition_table
	cp build/flash_args $(DIST_DIR)/
	cp build/camera_viewfinder.bin $(DIST_DIR)/
	cp build/bootloader/bootloader.bin $(DIST_DIR)/bootloader/
	cp build/partition_table/partition-table.bin $(DIST_DIR)/partition_table/
	@echo "\nFlash with:\n  cd $(DIST_DIR) && esptool.py --chip esp32s3 -b 460800 write_flash @flash_args"

docker-shell:
	$(DOCKER_RUN) -it $(IMAGE) bash -lc '$(IDF_CMD) && exec bash'

clean:
	rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
