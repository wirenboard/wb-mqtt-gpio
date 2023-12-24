#!/bin/bash

SYS_CONFFILE="/var/lib/wb-mqtt-gpio/conf.d/system.conf"
SCHEMA_FILE="/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.schema.json"
CONFED_SCHEMA_FILE="/var/lib/wb-mqtt-confed/schemas/wb-mqtt-gpio.schema.json"

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
    input_first=1
    output_first=1

    for gpiochip in /sys/bus/gpio/devices/gpiochip*; do
        if [[ -f "$gpiochip/of_node/phandle" ]]; then
            phandle=$(bin2ulong < "$gpiochip/of_node/phandle")
            chip_num=$(echo $(basename $gpiochip) | sed 's/gpiochip//g')
            PHANDLE_MAP[$phandle]=$chip_num
        fi
    done

    item_names=""
    input_names=""
    output_names=""
    disconnected=0
    for gpioname in  $(of_node_children "$node" | sort); do
        gpio="$(of_get_prop_gpio "$node/$gpioname" "io-gpios")"
        item_phandle=$(of_get_prop_ulong "$node/$gpioname" "io-gpios" | awk '{print $1}')
        item_chip_num=${PHANDLE_MAP[$item_phandle]}
        if [[ -z $item_chip_num ]]; then
            (( disconnected = disconnected + 1 ))
            gpiochip_path="disconnected_$disconnected"
        else
            gpiochip_path="/dev/gpiochip$item_chip_num"
        fi
        base=0
        pin=0
        of_gpio_unpack "$gpio" base pin
        direction=$(of_has_prop "$node/$gpioname" "input" && echo -n "input" || echo -n "output")
        ITEM="{ \
            \"name\" : \"$gpioname\", \
            \"gpio\" : {\"chip\" : \"$gpiochip_path\", \"offset\" : $pin}, \
            \"direction\" : \"$direction\", \
            \"inverted\" : $(of_gpio_is_inverted "$gpio" && echo -n "true" || echo -n "false"), \
            \"initial_state\": $(of_has_prop "$node/$gpioname" "output-high" && echo -n "true" || echo -n "false") ,\
            \"order\" : $(of_has_prop "$node/$gpioname" "sort-order" && echo -n $(of_get_prop_ulong "$node/$gpioname" "sort-order") || echo -n 0) }"
        
        if (( first )); then
            first=0
        else
            GPIOSYSCONF="$GPIOSYSCONF,"
            item_names="$item_names,"
        fi

        if [ $direction == "input" ]; then
            if (( input_first )); then
                input_first=0
            else
                input_names="$input_names,"
            fi
            input_names="$input_names \"$gpioname\""
        else
            if (( output_first )); then
                output_first=0
            else
                output_names="$output_names,"
            fi
            output_names="$output_names \"$gpioname\""
        fi

        GPIOSYSCONF="$GPIOSYSCONF $ITEM"
        item_names="$item_names \"$gpioname\""

    done

    GPIOSYSCONF="$GPIOSYSCONF ]}"
    echo "$GPIOSYSCONF" | jq '.channels|=sort_by(.order)|.channels|=map(del(.order))' > ${SYS_CONFFILE}

    custom_channels_filter=".definitions.gpio_channel.not.properties.name.enum=[$item_names]"
    undefined_channels_filter=".definitions.undefined_channel.not.properties.name.enum=[$item_names]"
    system_inputs_filter=".definitions.system_input.properties.name.enum=[$input_names]"
    system_outputs_filter=".definitions.system_output.properties.name.enum=[$output_names]"
    cat $SCHEMA_FILE | jq "$undefined_channels_filter|$custom_channels_filter|$system_inputs_filter|$system_outputs_filter" > ${CONFED_SCHEMA_FILE}
else
	echo "/sys/firmware/devicetree/base/wirenboard/gpios is missing"
fi
