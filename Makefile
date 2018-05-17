ifeq ($(DEB_TARGET_ARCH),armel)
CROSS_COMPILE=arm-linux-gnueabi-
endif

CXX=$(CROSS_COMPILE)g++
CXX_PATH := $(shell which $(CROSS_COMPILE)g++-4.7)

CC=$(CROSS_COMPILE)gcc
CC_PATH := $(shell which $(CROSS_COMPILE)gcc-4.7)

ifneq ($(CXX_PATH),)
	CXX=$(CROSS_COMPILE)g++-4.7
endif

ifneq ($(CC_PATH),)
	CC=$(CROSS_COMPILE)gcc-4.7
endif

DEBUG_CFLAGS=-Wall -ggdb -rdynamic -std=c++11 -O0 -I.
RELEASE_CFLAGS=-Wall -DNDEBUG -std=c++11 -Os -I.
TESTING_CFLAGS=-Wall -std=c++11 -Os -I.	# release with asserts
CFLAGS=$(if $(DEBUG), $(DEBUG_CFLAGS), $(if $(TESTING), $(TESTING_CFLAGS), $(RELEASE_CFLAGS)))
LDFLAGS= -ljsoncpp -lwbmqtt1 -lpthread

GPIO_BIN=wb-homa-gpio

GPIO_SOURCES= 			\
  gpio_driver.cpp		\
  gpio_chip_driver.cpp	\
  gpio_chip.cpp 		\
  gpio_line.cpp 		\
  gpio_counter.cpp		\
  config.cpp 			\
  utils.cpp 			\
  types.cpp 			\
  log.cpp 				\
  exceptions.cpp

GPIO_OBJECTS=$(GPIO_SOURCES:.cpp=.o)
GPIO_BIN=wb-homa-gpio


TEST_BIN=$(GPIO_BIN)-test
TEST_DIR=test
GPIO_TEST_SOURCES=                      \
  $(TEST_DIR)/test_main.cpp	            \
  $(TEST_DIR)/gpio_chip_driver_test.cpp \

GPIO_TEST_OBJECTS=$(GPIO_TEST_SOURCES:.cpp=.o)
TEST_LIBS=-lgtest -lwbmqtt_test_utils


all : $(GPIO_BIN)

# GPIO
%.o : %.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(GPIO_BIN) : main.o $(GPIO_OBJECTS)
	${CXX} $^ ${LDFLAGS} -o $@

$(TEST_DIR)/$(TEST_BIN): $(GPIO_OBJECTS) $(GPIO_TEST_OBJECTS)
	${CXX} $^ $(LDFLAGS) $(TEST_LIBS) -o $@


test: $(TEST_DIR)/$(TEST_BIN)

	rm -f $(TEST_DIR)/*.dat.out
    # if [ "$(shell arch)" = "armv7l" ]; then \
    #         $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || $(TEST_DIR)/abt.sh show; \
    #     else \
    #         valgrind --error-exitcode=180 -q $(TEST_DIR)/$(TEST_BIN) $(TEST_ARGS) || \
    #         if [ $$? = 180 ]; then \
    #             echo "*** VALGRIND DETECTED ERRORS ***" 1>& 2; \
    #             exit 1; \
    #         else $(TEST_DIR)/abt.sh show; exit 1; fi; \
    #     fi
clean :
	-rm -f *.o $(W1_BIN)
	-rm -f $(TEST_DIR)/*.o $(TEST_DIR)/$(TEST_BIN)

install: all
	install -d $(DESTDIR)
	install -d $(DESTDIR)/etc
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/share/wb-homa-gpio
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	install -d $(DESTDIR)/var/lib/wb-homa-gpio
	mkdir -p $(DESTDIR)/etc/wb-configs.d

	install -m 0644  config.json.wb52 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wb52
	install -m 0644  config.json.wb55 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wb55
	install -m 0644  config.json.wb58 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wb58
	install -m 0644  config.json.wb60 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wb60
	install -m 0644  config.json.wb61 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wb61
	install -m 0644  config.json.wbsh5 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wbsh5
	install -m 0644  config.json.wbsh4 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wbsh4
	install -m 0644  config.json.wbsh3 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wbsh3
	install -m 0644  config.json.default $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.default
	install -m 0644  config.json.mka3 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.mka3
	install -m 0644  config.json.cqc10 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.cqc10
	install -m 0755  $(GPIO_BIN) $(DESTDIR)/usr/bin/$(GPIO_BIN)
	install -m 0644  wb-homa-gpio.schema.json $(DESTDIR)/usr/share/wb-mqtt-confed/schemas/wb-homa-gpio.schema.json
	install -m 0644  wb-homa-gpio.wbconfigs $(DESTDIR)/etc/wb-configs.d/13wb-homa-gpio


.PHONY: all clean
