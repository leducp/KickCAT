BUILD_DIR ?= build
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra
PKGCONFIG ?= pkg-config

KICKCAT_CFLAGS := $(shell $(PKGCONFIG) --cflags kickcat)
KICKCAT_LIBS   := $(shell $(PKGCONFIG) --libs --static kickcat)

ifeq ($(KICKCAT_LIBS),)
$(error pkg-config could not resolve kickcat — check PKG_CONFIG_PATH)
endif

$(BUILD_DIR)/consumer: ../main.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(KICKCAT_CFLAGS) $< $(KICKCAT_LIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
