CC := gcc
CFLAGS := -g -Wall -Wimplicit-fallthrough
CFLAGSOPT := -O3 -Wall -Wimplicit-fallthrough
CPPFLAGS := -I/opt/homebrew/include -MP -MMD
LDFLAGS := $(shell sdl2-config --libs) -lm -lz

BUILD_DIR := build
RELEASE_DIR := release
SRC_DIR := src

TARGET_EXEC := ntremu

SRCS := $(basename $(notdir $(wildcard $(SRC_DIR)/*.c)))
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
OBJS_RELEASE := $(SRCS:%=$(RELEASE_DIR)/%.o)
DEPS_RELEASE := $(OBJS_RELEASE:.o=.d)

.PHONY: debug
debug: $(BUILD_DIR)/$(TARGET_EXEC)

.PHONY: release
release: $(RELEASE_DIR)/$(TARGET_EXEC)

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(CPPFLAGS) $^ $(LDFLAGS)
	cp $(BUILD_DIR)/$(TARGET_EXEC) ./$(TARGET_EXEC)-dbg

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RELEASE_DIR)/$(TARGET_EXEC): $(OBJS_RELEASE)
	$(CC) -o $@ $(CFLAGSOPT) $(CPPFLAGS) $^ $(LDFLAGS)
	cp $(RELEASE_DIR)/$(TARGET_EXEC) ./$(TARGET_EXEC)

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(RELEASE_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGSOPT) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(RELEASE_DIR)
	rm $(TARGET_EXEC)-dbg
	rm $(TARGET_EXEC)

-include $(DEPS)
-include $(DEPS_RELEASE)
