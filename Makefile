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

COMMON_SRCS := $(shell find $(SRC_DIR) -name *.cpp -and -not -name main.cpp)
COMMON_OBJS := $(COMMON_SRCS:%=$(BUILD_DIR)/%.o)

CXXFLAGS = -Wall -std=c++17 -I$(SRC_DIR)
LDFLAGS = -lwbmqtt1 -lpthread

ifeq ($(DEBUG), 1)
	CXXFLAGS += -O0 -ggdb -rdynamic -fprofile-arcs -ftest-coverage
	LDFLAGS += -lgcov
else
	CXXFLAGS += -Os -DNDEBUG
endif

TEST_DIR = test
TEST_SRCS := $(shell find $(TEST_DIR) -name *.cpp)
TEST_OBJS := $(TEST_SRCS:%=$(BUILD_DIR)/%.o)
TEST_BIN = test
TEST_LIBS = -lgtest -lwbmqtt_test_utils

export TEST_DIR_ABS = $(shell pwd)/$(TEST_DIR)

all : $(GPIO_BIN)

# GPIO
$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(GPIO_BIN) : $(COMMON_OBJS) $(BUILD_DIR)/src/main.cpp.o
	$(CXX) $^ $(LDFLAGS) -o $(BUILD_DIR)/$@

$(BUILD_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN): $(COMMON_OBJS) $(TEST_OBJS)
	$(CXX) $^ $(LDFLAGS) $(TEST_LIBS) -o $@

test: $(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN)
	rm -f $(TEST_DIR)/*.dat.out
	if [ "$(shell arch)" != "armv7l" ] && [ "$(CROSS_COMPILE)" = "" ] || [ "$(CROSS_COMPILE)" = "x86_64-linux-gnu-" ]; then \
		valgrind --error-exitcode=180 -q $(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
		if [ $$? = 180 ]; then \
			echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
			exit 1; \
		else $(TEST_DIR)/abt.sh show; exit 1; fi; \
	else \
		$(BUILD_DIR)/$(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || { $(TEST_DIR)/abt.sh show; exit 1; } \
	fi

lcov: test
ifeq ($(DEBUG), 1)
	geninfo --no-external -b . -o $(BUILD_DIR)/coverage.info $(BUILD_DIR)/src
	genhtml $(BUILD_DIR)/coverage.info -o $(BUILD_DIR)/cov_html
endif

clean :
	-rm -rf build/release
	-rm -rf build/debug

install: all
	install -d $(DESTDIR)/var/lib/wb-mqtt-gpio/conf.d

	install -Dm0644 config.json.devicetree $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.devicetree
	install -Dm0644 config.json.wb52 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb52
	install -Dm0644 config.json.wb55 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb55
	install -Dm0644 config.json.wb58 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb58
	install -Dm0644 config.json.wb60 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb60
	install -Dm0644 config.json.wb61 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb61
	install -Dm0644 config.json.wbsh5 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh5
	install -Dm0644 config.json.wbsh4 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh4
	install -Dm0644 config.json.wbsh3 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh3
	install -Dm0644 config.json.default $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.default
	install -Dm0644 config.json.mka3 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.mka3
	install -Dm0644 config.json.cqc10 $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.cqc10

	install -Dm0755 $(BUILD_DIR)/$(GPIO_BIN) -t $(DESTDIR)$(PREFIX)/bin
	install -Dm0755 generate-system-config.sh -t $(DESTDIR)$(PREFIX)/lib/wb-mqtt-gpio

	install -Dm0644 wb-mqtt-gpio.schema.json -t $(DESTDIR)$(PREFIX)/share/wb-mqtt-gpio
	install -Dm0644 wb-mqtt-gpio.wbconfigs $(DESTDIR)/etc/wb-configs.d/13wb-mqtt-gpio


.PHONY: all clean test
