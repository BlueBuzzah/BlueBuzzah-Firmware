# BlueBuzzah Arduino Firmware - Build & Deploy Makefile
# =====================================================
#
# Usage:
#   make build       - Build firmware
#   make deploy      - Smart deploy with role assignment
#   make list-devices - Show connected devices
#   make monitor     - Open serial monitor (interactive)
#   make clean       - Clean build artifacts
#
# Deployment:
#   - 1 device:  Uploads firmware, prompts for role (P/S/N)
#   - 2 devices: Prompts for PRIMARY/SECONDARY assignment,
#                uploads to both, configures roles automatically
#
# Requirements:
#   - PlatformIO CLI (pio)
#   - Connected Adafruit Feather nRF52840 device(s)

.PHONY: build deploy deploy-primary deploy-secondary _interactive_deploy _deploy_with_role list-devices monitor clean help

# Default target
.DEFAULT_GOAL := help

# Colors for output
CYAN := \033[0;36m
GREEN := \033[0;32m
YELLOW := \033[0;33m
RED := \033[0;31m
NC := \033[0m # No Color

# PlatformIO command
PIO := pio

#------------------------------------------------------------------------------
# Help
#------------------------------------------------------------------------------
help:
	@echo ""
	@echo "$(CYAN)BlueBuzzah Arduino Firmware - Build & Deploy$(NC)"
	@echo "=============================================="
	@echo ""
	@echo "$(GREEN)Available targets:$(NC)"
	@echo "  make build           - Build firmware"
	@echo "  make deploy          - Smart deploy (detects devices, assigns roles)"
	@echo "  make deploy-primary  - Deploy to single device as PRIMARY"
	@echo "  make deploy-secondary - Deploy to single device as SECONDARY"
	@echo "  make list-devices    - Show connected Feather devices"
	@echo "  make monitor         - Open serial monitor"
	@echo "  make clean           - Clean build artifacts"
	@echo ""
	@echo "$(YELLOW)Deployment workflow:$(NC)"
	@echo "  1 device:  Uploads firmware, prompts for role"
	@echo "  2 devices: Prompts for PRIMARY/SECONDARY assignment,"
	@echo "             uploads to both, configures roles automatically"
	@echo ""
	@echo "$(YELLOW)Query current role:$(NC)"
	@echo "  Type: GET_ROLE (in serial monitor)"
	@echo ""

#------------------------------------------------------------------------------
# Build
#------------------------------------------------------------------------------
build:
	@echo "$(CYAN)Building firmware...$(NC)"
	$(PIO) run
	@echo "$(GREEN)Build complete!$(NC)"

#------------------------------------------------------------------------------
# Deploy (Build + Upload + Configure Roles)
#------------------------------------------------------------------------------
deploy: build
	@echo ""
	@echo "$(CYAN)=== Deploying firmware ===$(NC)"
	@echo ""
	@$(MAKE) _interactive_deploy

#------------------------------------------------------------------------------
# Interactive Deploy with Role Assignment (internal target)
#------------------------------------------------------------------------------
_interactive_deploy:
	@echo "$(YELLOW)Scanning for connected devices...$(NC)"
	@echo ""
	@DEVICES=$$(ls /dev/cu.usbmodem* 2>/dev/null || true); \
	if [ -z "$$DEVICES" ]; then \
		echo "$(RED)No devices found!$(NC)"; \
		echo "Make sure your Feather nRF52840 is connected via USB."; \
		exit 1; \
	fi; \
	DEVICE_COUNT=$$(echo "$$DEVICES" | wc -w | tr -d ' '); \
	if [ "$$DEVICE_COUNT" = "1" ]; then \
		SELECTED="$$DEVICES"; \
		echo "$(GREEN)Found 1 device: $$SELECTED$(NC)"; \
		echo ""; \
		echo "$(YELLOW)Uploading firmware...$(NC)"; \
		$(PIO) run -t upload --upload-port "$$SELECTED" || exit 1; \
		echo ""; \
		read -p "Configure role? [P]rimary, [S]econdary, or [N]one: " role_choice; \
		if [ "$$role_choice" = "P" ] || [ "$$role_choice" = "p" ]; then \
			echo "$(YELLOW)Waiting for device to restart...$(NC)"; \
			sleep 3; \
			stty -f "$$SELECTED" 115200 raw -echo 2>/dev/null || true; \
			printf "SET_ROLE:PRIMARY\n" > "$$SELECTED"; \
			sleep 1; \
			echo "$(GREEN)=== Device configured as PRIMARY! ===$(NC)"; \
		elif [ "$$role_choice" = "S" ] || [ "$$role_choice" = "s" ]; then \
			echo "$(YELLOW)Waiting for device to restart...$(NC)"; \
			sleep 3; \
			stty -f "$$SELECTED" 115200 raw -echo 2>/dev/null || true; \
			printf "SET_ROLE:SECONDARY\n" > "$$SELECTED"; \
			sleep 1; \
			echo "$(GREEN)=== Device configured as SECONDARY! ===$(NC)"; \
		else \
			echo "$(GREEN)=== Deploy complete (no role configured) ===$(NC)"; \
		fi; \
	else \
		echo "$(GREEN)Found $$DEVICE_COUNT devices:$(NC)"; \
		echo ""; \
		i=1; \
		for dev in $$DEVICES; do \
			echo "  $$i) $$dev"; \
			i=$$((i + 1)); \
		done; \
		echo ""; \
		echo "$(CYAN)Assign device roles for deployment:$(NC)"; \
		echo ""; \
		read -p "Which device is PRIMARY? (enter number, or 'q' to quit): " primary_choice; \
		if [ "$$primary_choice" = "q" ]; then \
			echo "Aborted."; \
			exit 0; \
		fi; \
		read -p "Which device is SECONDARY? (enter number, or 'q' to quit): " secondary_choice; \
		if [ "$$secondary_choice" = "q" ]; then \
			echo "Aborted."; \
			exit 0; \
		fi; \
		if [ "$$primary_choice" = "$$secondary_choice" ]; then \
			echo "$(RED)Error: PRIMARY and SECONDARY must be different devices!$(NC)"; \
			exit 1; \
		fi; \
		PRIMARY_DEV=""; \
		SECONDARY_DEV=""; \
		i=1; \
		for dev in $$DEVICES; do \
			if [ "$$i" = "$$primary_choice" ]; then \
				PRIMARY_DEV="$$dev"; \
			fi; \
			if [ "$$i" = "$$secondary_choice" ]; then \
				SECONDARY_DEV="$$dev"; \
			fi; \
			i=$$((i + 1)); \
		done; \
		if [ -z "$$PRIMARY_DEV" ]; then \
			echo "$(RED)Invalid PRIMARY selection!$(NC)"; \
			exit 1; \
		fi; \
		if [ -z "$$SECONDARY_DEV" ]; then \
			echo "$(RED)Invalid SECONDARY selection!$(NC)"; \
			exit 1; \
		fi; \
		echo ""; \
		echo "$(CYAN)Deployment plan:$(NC)"; \
		echo "  PRIMARY:   $$PRIMARY_DEV"; \
		echo "  SECONDARY: $$SECONDARY_DEV"; \
		echo ""; \
		read -p "Proceed? [y/N]: " confirm; \
		if [ "$$confirm" != "y" ] && [ "$$confirm" != "Y" ]; then \
			echo "Aborted."; \
			exit 0; \
		fi; \
		echo ""; \
		echo "$(YELLOW)[1/4] Uploading to PRIMARY ($$PRIMARY_DEV)...$(NC)"; \
		$(PIO) run -t upload --upload-port "$$PRIMARY_DEV" || exit 1; \
		echo ""; \
		echo "$(YELLOW)[2/4] Uploading to SECONDARY ($$SECONDARY_DEV)...$(NC)"; \
		$(PIO) run -t upload --upload-port "$$SECONDARY_DEV" || exit 1; \
		echo ""; \
		echo "$(YELLOW)[3/4] Waiting for devices to restart...$(NC)"; \
		sleep 3; \
		echo "$(YELLOW)[4/4] Configuring roles...$(NC)"; \
		stty -f "$$PRIMARY_DEV" 115200 raw -echo 2>/dev/null || true; \
		printf "SET_ROLE:PRIMARY\n" > "$$PRIMARY_DEV"; \
		sleep 1; \
		stty -f "$$SECONDARY_DEV" 115200 raw -echo 2>/dev/null || true; \
		printf "SET_ROLE:SECONDARY\n" > "$$SECONDARY_DEV"; \
		sleep 1; \
		echo ""; \
		echo "$(GREEN)=== Both devices deployed and configured! ===$(NC)"; \
		echo "  PRIMARY:   $$PRIMARY_DEV"; \
		echo "  SECONDARY: $$SECONDARY_DEV"; \
	fi; \
	echo ""

#------------------------------------------------------------------------------
# Deploy as PRIMARY (Build + Upload + Configure)
#------------------------------------------------------------------------------
deploy-primary: build
	@echo ""
	@echo "$(CYAN)=== Deploying as PRIMARY ===$(NC)"
	@echo ""
	@$(MAKE) _deploy_with_role ROLE=PRIMARY

#------------------------------------------------------------------------------
# Deploy as SECONDARY (Build + Upload + Configure)
#------------------------------------------------------------------------------
deploy-secondary: build
	@echo ""
	@echo "$(CYAN)=== Deploying as SECONDARY ===$(NC)"
	@echo ""
	@$(MAKE) _deploy_with_role ROLE=SECONDARY

#------------------------------------------------------------------------------
# Deploy with Role Configuration (internal target)
#------------------------------------------------------------------------------
_deploy_with_role:
	@echo "$(YELLOW)Scanning for connected devices...$(NC)"
	@echo ""
	@DEVICES=$$(ls /dev/cu.usbmodem* 2>/dev/null || true); \
	if [ -z "$$DEVICES" ]; then \
		echo "$(RED)No devices found!$(NC)"; \
		echo "Make sure your Feather nRF52840 is connected via USB."; \
		exit 1; \
	fi; \
	DEVICE_COUNT=$$(echo "$$DEVICES" | wc -w | tr -d ' '); \
	if [ "$$DEVICE_COUNT" = "1" ]; then \
		SELECTED="$$DEVICES"; \
		echo "$(GREEN)Found device: $$SELECTED$(NC)"; \
	else \
		echo "$(GREEN)Found devices:$(NC)"; \
		echo ""; \
		i=1; \
		for dev in $$DEVICES; do \
			echo "  $$i) $$dev"; \
			i=$$((i + 1)); \
		done; \
		echo ""; \
		read -p "Select device number (or 'q' to quit): " choice; \
		if [ "$$choice" = "q" ]; then \
			echo "Aborted."; \
			exit 0; \
		fi; \
		i=1; \
		SELECTED=""; \
		for dev in $$DEVICES; do \
			if [ "$$i" = "$$choice" ]; then \
				SELECTED="$$dev"; \
				break; \
			fi; \
			i=$$((i + 1)); \
		done; \
		if [ -z "$$SELECTED" ]; then \
			echo "$(RED)Invalid selection!$(NC)"; \
			exit 1; \
		fi; \
	fi; \
	echo ""; \
	echo "$(YELLOW)Uploading firmware to $$SELECTED...$(NC)"; \
	$(PIO) run -t upload --upload-port "$$SELECTED" || exit 1; \
	echo ""; \
	echo "$(YELLOW)Waiting for device to restart...$(NC)"; \
	sleep 3; \
	echo "$(YELLOW)Configuring role as $(ROLE)...$(NC)"; \
	stty -f "$$SELECTED" 115200 raw -echo 2>/dev/null || true; \
	printf "SET_ROLE:$(ROLE)\n" > "$$SELECTED"; \
	sleep 1; \
	echo ""; \
	echo "$(GREEN)=== Device configured as $(ROLE)! ===$(NC)"; \
	echo ""

#------------------------------------------------------------------------------
# List Devices
#------------------------------------------------------------------------------
list-devices:
	@echo ""
	@echo "$(CYAN)Connected Feather devices:$(NC)"
	@echo ""
	@DEVICES=$$(ls /dev/cu.usbmodem* 2>/dev/null || true); \
	if [ -z "$$DEVICES" ]; then \
		echo "$(YELLOW)No devices found.$(NC)"; \
		echo "Make sure your Feather nRF52840 is connected via USB."; \
	else \
		for dev in $$DEVICES; do \
			echo "  - $$dev"; \
		done; \
	fi
	@echo ""

#------------------------------------------------------------------------------
# Serial Monitor
#------------------------------------------------------------------------------
monitor:
	@echo ""
	@echo "$(CYAN)Opening serial monitor...$(NC)"
	@echo ""
	@DEVICES=$$(ls /dev/cu.usbmodem* 2>/dev/null || true); \
	if [ -z "$$DEVICES" ]; then \
		echo "$(RED)No devices found!$(NC)"; \
		exit 1; \
	fi; \
	DEVICE_COUNT=$$(echo "$$DEVICES" | wc -w | tr -d ' '); \
	if [ "$$DEVICE_COUNT" = "1" ]; then \
		SELECTED="$$DEVICES"; \
		echo "Using: $$SELECTED"; \
	else \
		echo "$(GREEN)Found devices:$(NC)"; \
		i=1; \
		for dev in $$DEVICES; do \
			echo "  $$i) $$dev"; \
			i=$$((i + 1)); \
		done; \
		echo ""; \
		read -p "Select device number: " choice; \
		i=1; \
		for dev in $$DEVICES; do \
			if [ "$$i" = "$$choice" ]; then \
				SELECTED="$$dev"; \
				break; \
			fi; \
			i=$$((i + 1)); \
		done; \
	fi; \
	echo ""; \
	echo "$(YELLOW)Commands: SET_ROLE:PRIMARY | SET_ROLE:SECONDARY | GET_ROLE$(NC)"; \
	echo "$(YELLOW)Press Ctrl+C to exit$(NC)"; \
	echo ""; \
	$(PIO) device monitor --port "$$SELECTED" --baud 115200

#------------------------------------------------------------------------------
# Clean
#------------------------------------------------------------------------------
clean:
	@echo "$(CYAN)Cleaning build artifacts...$(NC)"
	$(PIO) run -t clean
	@echo "$(GREEN)Clean complete!$(NC)"
