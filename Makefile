.DEFAULT_GOAL := help

VENV := .venv
BIN := .pio/build/esp32c6/firmware.factory.bin
IMAGE := uni-t-ble-firmware
SERVER_DIR ?= ../uni-t-sound-node-server

.PHONY: help tools config build upload monitor flash clean deploy deploy-server deploy-device

help: ## Show available project commands
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-14s %s\n", $$1, $$2}'

tools: ## Install the small host tools (esptool, serial monitor) into ./.venv
	@test -x $(VENV)/bin/python && $(VENV)/bin/python -m esptool version >/dev/null 2>&1 && $(VENV)/bin/python -m platformio --version >/dev/null 2>&1 || { python3 -m venv $(VENV) && $(VENV)/bin/python -m pip install -q esptool platformio; }

config: ## Create include/secrets.h from the example if missing
	@test -f include/secrets.h || { cp include/secrets.example.h include/secrets.h; echo "Created include/secrets.h - edit it before flashing"; }

build: config ## Compile the firmware in Docker
	docker build --build-arg PIO_UID=$(shell id -u) -t $(IMAGE) .
	docker run --rm -v "$(CURDIR)":/workspace -w /workspace $(IMAGE) run

upload: build tools ## Build in Docker, then flash the image over host USB
	$(VENV)/bin/python -m esptool --chip esp32c6 write-flash 0x0 $(BIN)

monitor: tools ## Open the serial monitor (115200 baud)
	$(VENV)/bin/python -m platformio device monitor -d $(VENV) -b 115200

flash: upload ## Flash, then open the serial monitor
	$(MAKE) monitor

deploy: ## Deploy the receiver server, then build and flash this ESP32
	$(MAKE) deploy-server && $(MAKE) deploy-device

deploy-server: ## Deploy the receiver server from SERVER_DIR
	$(MAKE) -C "$(SERVER_DIR)" deploy

deploy-device: upload ## Build and flash this ESP32

clean: ## Remove firmware build output
	rm -rf .pio/build
