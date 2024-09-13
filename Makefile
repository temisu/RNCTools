BUILD_DIR := build

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

rnc: $(BUILD_DIR)
	gcc -Wall -Wextra -DCOMPRESSOR -o $(BUILD_DIR)/rnc rnc.c dernc.c

dernc: $(BUILD_DIR)
	gcc -Wall -Wextra -DMAIN -o $(BUILD_DIR)/dernc dernc.c

all: rnc dernc

clean:
	rm -rf $(BUILD_DIR)

.DEFAULT_GOAL := all
.PHONY: all clean