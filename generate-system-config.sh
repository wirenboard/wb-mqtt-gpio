#!/bin/bash

SYS_CONFFILE="/etc/wb-mqtt-gpio.conf.d/system.conf"

if [[ -d /sys/firmware/devicetree/base/wirenboard/gpios ]]; then
    source "/usr/lib/wb-utils/common.sh"
    wb_source "of"
    wb_source "wb_env_of"
    WB_ENV_CACHE="${WB_ENV_CACHE:-/var/run/wb_env.cache}"

    if [[ -e "$WB_ENV_CACHE" ]]; then
        set -a
        source "$WB_ENV_CACHE"
        set +a
    else
        declare -A OF_GPIOCHIPS
        of_find_gpiochips
        declare -p OF_GPIOCHIPS
    fi

    WB_OF_ROOT="/wirenboard"
    node="${WB_OF_ROOT}/gpios"

    declare -A firstinchip
    declare -A PHANDLE_MAP

    GPIOSYSCONF='{ "channels": ['
    first=1

    for gpiochip in /sys/bus/gpio/devices/gpiochip*; do
        if [[ -f "$gpiochip/of_node/phandle" ]]; then
            phandle=$(bin2ulong < "$gpiochip/of_node/phandle")
            chip_num=$(echo $(basename $gpiochip) | sed 's/gpiochip//g')
            PHANDLE_MAP[$phandle]=$chip_num
        fi
    done

    for gpioname in  $(of_node_children "$node" | sort); do
        gpio="$(of_get_prop_gpio "$node/$gpioname" "io-gpios")"
        item_phandle=$(of_get_prop_ulong "$node/$gpioname" "io-gpios" | awk '{print $1}')
        item_chip_num=${PHANDLE_MAP[$item_phandle]}
        base=0
        pin=0
        of_gpio_unpack "$gpio" base pin
        ITEM="{ \
            \"name\" : \"$gpioname\", \
            \"gpio\" : {\"chip\" : \"/dev/gpiochip$item_chip_num\", \"offset\" : $pin}, \
            \"direction\" : \"$(of_has_prop "$node/$gpioname" "input" && echo -n "input" || echo -n "output")\", \
            \"inverted\" : $(of_gpio_is_inverted "$gpio" && echo -n "true" || echo -n "false"), \
            \"initial_state\": $(of_has_prop "$node/$gpioname" "output-high" && echo -n "true" || echo -n "false") ,\
            \"order\" : $(of_has_prop "$node/$gpioname" "sort-order" && echo -n $(of_get_prop_ulong "$node/$gpioname" "sort-order") || echo -n 0) }"
        
        if (( first )); then
            first=0
        else
            GPIOSYSCONF="$GPIOSYSCONF,"
        fi

        GPIOSYSCONF="$GPIOSYSCONF $ITEM"
    done

    GPIOSYSCONF="$GPIOSYSCONF ]}"
    echo "$GPIOSYSCONF" | jq '.channels|=sort_by(.order)|.channels|=map(del(.order))' > ${SYS_CONFFILE}
else
	echo "/sys/firmware/devicetree/base/wirenboard/analog-inputs is missing"
fi
