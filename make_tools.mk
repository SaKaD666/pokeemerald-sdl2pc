# This controls building executables in the `tools` folder.
# Can be invoked through the `Makefile` or standalone.

MAKEFLAGS += --no-print-directory

# Keep all tools in the cleanup list so stale binaries are removed.
TOOLS_DIR := tools
TOOL_NAMES := bin2c gbagfx jsonproc mapjson mid2agb preproc rsfont scaninc wav2agb

BUILD_TOOLDIRS := $(TOOL_NAMES:%=$(TOOLS_DIR)/%)
CLEAN_TOOLDIRS := $(TOOL_NAMES:%=$(TOOLS_DIR)/%)

# Tool making doesnt require a pokeemerald dependency scan.
RULES_NO_SCAN += tools check-tools clean-tools $(BUILD_TOOLDIRS) $(CLEAN_TOOLDIRS)
.PHONY: $(RULES_NO_SCAN)

tools: $(BUILD_TOOLDIRS)

$(BUILD_TOOLDIRS):
	@$(MAKE) -C $@

clean-tools:
	@$(foreach tooldir,$(CLEAN_TOOLDIRS),$(MAKE) clean -C $(tooldir);)
