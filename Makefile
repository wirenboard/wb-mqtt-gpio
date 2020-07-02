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

DEBUG_CFLAGS=-Wall -ggdb -rdynamic -std=c++14 -O0 -I.
RELEASE_CFLAGS=-Wall -DNDEBUG -std=c++14 -Os -I.
TESTING_CFLAGS=-Wall -std=c++14 -Os -I.	# release with asserts
CFLAGS=$(if $(DEBUG), $(DEBUG_CFLAGS), $(if $(TESTING), $(TESTING_CFLAGS), $(RELEASE_CFLAGS)))
LDFLAGS= -ljsoncpp -lwbmqtt1 -lpthread

GPIO_BIN=wb-mqtt-gpio

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

all : $(GPIO_BIN)

# GPIO
%.o : %.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(GPIO_BIN) : main.o $(GPIO_OBJECTS)
	${CXX} $^ ${LDFLAGS} -o $@

clean :
	-rm -f *.o $(GPIO_BIN)

install: all
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/share/wb-mqtt-gpio
	install -d $(DESTDIR)/usr/share/wb-mqtt-confed/schemas
	install -d $(DESTDIR)/var/lib/wb-mqtt-gpio
	install -d $(DESTDIR)/etc/wb-configs.d

	install -m 0644  config.json.devicetree $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.devicetree
	install -m 0644  config.json.wb52 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb52
	install -m 0644  config.json.wb55 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb55
	install -m 0644  config.json.wb58 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb58
	install -m 0644  config.json.wb60 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb60
	install -m 0644  config.json.wb61 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wb61
	install -m 0644  config.json.wbsh5 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh5
	install -m 0644  config.json.wbsh4 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh4
	install -m 0644  config.json.wbsh3 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.wbsh3
	install -m 0644  config.json.default $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.default
	install -m 0644  config.json.mka3 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.mka3
	install -m 0644  config.json.cqc10 $(DESTDIR)/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.conf.cqc10
	install -m 0755  $(GPIO_BIN) $(DESTDIR)/usr/bin/$(GPIO_BIN)
	install -m 0644  wb-mqtt-gpio.schema.json $(DESTDIR)/usr/share/wb-mqtt-confed/schemas/wb-mqtt-gpio.schema.json
	install -m 0644  wb-mqtt-gpio.wbconfigs $(DESTDIR)/etc/wb-configs.d/13wb-mqtt-gpio


.PHONY: all clean
