ifneq ($(DEB_HOST_MULTIARCH),)
	CROSS_COMPILE ?= $(DEB_HOST_MULTIARCH)-
endif

ifeq ($(origin CC),default)
	CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
	CXX := $(CROSS_COMPILE)g++
endif

ifeq ($(DEBUG),)
	BUILD_DIR ?= build/release
else
	BUILD_DIR ?= build/debug
endif

PREFIX = /usr

GPIO_BIN = wb-mqtt-gpio
SRC_DIR = src

COMMON_SRCS := $(shell find $(SRC_DIR) -name "*.cpp" -and -not -name main.cpp)
COMMON_OBJS := $(COMMON_SRCS:%=$(BUILD_DIR)/%.o)

CXXFLAGS = -Wall -std=c++17 -I$(SRC_DIR)
LDFLAGS = -lwbmqtt1 -lpthread

ifeq ($(DEBUG),)
	CXXFLAGS += -Os -DNDEBUG
else
	CXXFLAGS += -O0 -ggdb -rdynamic --coverage
	LDFLAGS += --coverage
endif

TEST_DIR = test
TEST_SRCS := $(shell find $(TEST_DIR) -name "*.cpp")
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)
TEST_BIN = test
TEST_LIBS = -lgtest -lwbmqtt_test_utils -lgmock

export TEST_DIR_ABS = $(shell pwd)/$(TEST_DIR)

VALGRIND_FLAGS = --error-exitcode=180 -q

COV_REPORT ?= $(BUILD_DIR)/cov.html
GCOVR_FLAGS := --html $(COV_REPORT)
ifneq ($(COV_FAIL_UNDER),)
	GCOVR_FLAGS += --fail-under-line $(COV_FAIL_UNDER)
endif

all : $(GPIO_BIN)

# GPIO
$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(GPIO_BIN) : $(COMMON_OBJS) $(BUILD_DIR)/src/main.cpp.o
	$(CXX) $^ $(LDFLAGS) -o $(BUILD_DIR)/$@

$(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN): $(COMMON_OBJS) $(TEST_OBJS)
	$(CXX) $^ $(LDFLAGS) $(TEST_LIBS) -o $@

test: $(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" != "armv7l" ] && [ "$(CROSS_COMPILE)" = "" ] || [ "$(CROSS_COMPILE)" = "x86_64-linux-gnu-" ]; then \
		valgrind $(VALGRIND_FLAGS) $(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	else \
		$(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
	fi
ifneq ($(DEBUG),)
	gcovr $(GCOVR_FLAGS) $(BUILD_DIR)/$(SRC_DIR) $(BUILD_DIR)/$(TEST_DIR)
endif

clean :
	-rm -rf build

install: all
	install -d $(DESTDIR)/var/lib/wb-mqtt-gpio/conf.d

	for cfg in config.json.*; do \
		board=$${cfg##*.}; \
		install -Dm0644 $$cfg $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.$$board; \
	done

	install -Dm0755 $(BUILD_DIR)/$(GPIO_BIN) -t $(DESTDIR)$(PREFIX)/bin
	install -Dm0755 generate-system-config.py -t $(DESTDIR)$(PREFIX)/lib/wb-mqtt-gpio

	install -Dm0644 wb-mqtt-gpio.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio
	install -Dm0644 wb-mqtt-gpio.wbconfigs $(DESTDIR)/etc/wb-configs.d/13wb-mqtt-gpio


.PHONY: all clean test
