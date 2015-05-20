CXX=g++
CXX_PATH := $(shell which g++-4.7)

CC=gcc
CC_PATH := $(shell which gcc-4.7)

ifneq ($(CXX_PATH),)
	CXX=g++-4.7
endif

ifneq ($(CC_PATH),)
	CC=gcc-4.7
endif

#CFLAGS=-Wall -ggdb -std=c++0x -O0 -I.
CFLAGS=-Wall -std=c++0x -Os -I.
LDFLAGS= -lmosquittopp -lmosquitto -ljsoncpp -lwbmqtt

GPIO_BIN=wb-homa-gpio

.PHONY: all clean

all : $(GPIO_BIN)


# GPIO
main.o : main.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

sysfs_gpio.o : sysfs_gpio.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

sysfs_gpio_base_counter.o : sysfs_gpio_base_counter.cpp
	${CXX} -c $< -o $@ ${CFLAGS}

$(GPIO_BIN) : main.o sysfs_gpio.o sysfs_gpio_base_counter.o
	${CXX} $^ ${LDFLAGS} -o $@

clean :
	-rm -f *.o $(GPIO_BIN)



install: all
	install -d $(DESTDIR)
	install -d $(DESTDIR)/etc
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/share/wb-homa-gpio

	install -m 0644  config.json.wbsh4 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wbsh4
	install -m 0644  config.json.wbsh3 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.wbsh3
	install -m 0644  config.json.default $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.default
	install -m 0644  config.json.mka3 $(DESTDIR)/usr/share/wb-homa-gpio/wb-homa-gpio.conf.mka3
	install -m 0755  $(GPIO_BIN) $(DESTDIR)/usr/bin/$(GPIO_BIN)
