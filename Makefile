

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
# Top-level module name and output file prefix
PROJ = CPU

# Target FPGA details (Default: iCE40HX1K-TQ144, e.g., iCEstick)
DEVICE    = up5k
FOOTPRINT = sg48
FREQ      = 12

# Source files
SRCS = $(wildcard *.v)
PCF  = icebreaker.pcf

# Toolchain executables
YOSYS   = yosys
NEXTPNR = nextpnr-ice40
ICEPACK = icepack
ICEPROG = iceprog

# Build output directory
BUILD_DIR = build

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
.PHONY: all synth pnr bitstream prog clean

all: bitstream

# Create build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Step 1: Synthesis (Yosys) -> JSON Netlist
$(BUILD_DIR)/$(PROJ).json: $(SRCS) | $(BUILD_DIR)
	$(YOSYS) -p "synth_ice40 -spram -top $(PROJ) -json $@; stat" $(SRCS)

# Step 2: Place and Route (nextpnr) -> ASCII Bitstream (.asc)
$(BUILD_DIR)/$(PROJ).asc: $(BUILD_DIR)/$(PROJ).json $(PCF)
	$(NEXTPNR) --$(DEVICE) --package $(FOOTPRINT) --pcf $(PCF) --json $< --asc $@ --freq $(FREQ) --pcf-allow-unconstrained

# Step 3: Packing (icepack) -> Binary Bitstream (.bin)
$(BUILD_DIR)/$(PROJ).bin: $(BUILD_DIR)/$(PROJ).asc
	$(ICEPACK) $< $@

# Alias target for building bitstream
bitstream: $(BUILD_DIR)/$(PROJ).bin

# Step 4: Program FPGA SRAM (iceprog)
prog: $(BUILD_DIR)/$(PROJ).bin
	$(ICEPROG) $<

# Clean up build artifacts
clean:
	rm -rf $(BUILD_DIR)