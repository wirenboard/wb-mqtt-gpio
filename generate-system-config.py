#!/usr/bin/env python3
import json
import os
import sys

SYS_CONFFILE = "/var/lib/wb-mqtt-gpio/conf.d/system.conf"
SCHEMA_FILE = "/usr/share/wb-mqtt-gpio/wb-mqtt-gpio.schema.json"
CONFED_SCHEMA_FILE = "/var/lib/wb-mqtt-confed/schemas/wb-mqtt-gpio.schema.json"

WB_OF_ROOT = "/wirenboard"


def __of_node_path(node):
    if node.startswith("/sys/firmware"):
        return str(os.path.realpath(node))
    return str(os.path.realpath(f"/proc/device-tree/{node}"))


def of_find_gpiochips():
    of_gpiochips = {}
    for entry in os.listdir("/sys/class/gpio"):
        if not entry.startswith("gpiochip"):
            continue
        gpiochip = os.path.join("/sys/class/gpio", entry)
        if not os.path.isfile(os.path.join(gpiochip, "device/of_node/phandle")):
            continue
        phandle = bin2ulong(os.path.join(gpiochip, "device/of_node/phandle"), single_value=True)
        with open(os.path.join(gpiochip, "base")) as f:
            base = int(f.read())
        of_gpiochips[phandle] = base
    return of_gpiochips


def of_has_prop(node, prop):
    return os.path.exists(os.path.join(__of_node_path(node), prop))


def of_get_prop_str(node, prop):
    filename = os.path.join(__of_node_path(node), prop)
    with open(filename, "r") as f:
        return f.read().strip("\x00")


def of_get_prop_ulong(node, prop):
    filename = os.path.join(__of_node_path(node), prop)
    return bin2ulong(filename)


def of_node_children(node):
    node = __of_node_path(node)
    return [entry for entry in os.listdir(node) if os.path.isdir(os.path.join(node, entry))]


def split_each(iterable, chunk_size):
    data = list(iterable).copy()
    while len(data) >= chunk_size:
        yield data[:chunk_size]
        data = data[chunk_size:]
    if data:
        yield data + [None] * (chunk_size - len(data))


def of_get_prop_gpio(of_gpiochips, node, prop="gpios"):
    xlate_type = "offset"
    if of_has_prop(node, "gpio-xlate-type"):
        xlate_type = of_get_prop_str(node, "gpio-xlate-type")
    values = of_get_prop_ulong(node, prop)
    for phandle, arg0, arg1, arg2 in split_each(values, 4):
        if xlate_type == "bank_pin":
            bank = arg0
            bank_pin = arg1
            attr = arg2
            pin = (int(bank) * 32) + int(bank_pin)
        elif xlate_type == "offset":
            attr = arg1
            pin = arg0
        else:
            raise RuntimeError(f"Unknown xlate type {xlate_type}")
        if phandle not in of_gpiochips:
            raise RuntimeError(f"Unknown phandle {phandle} (known phandles: {of_gpiochips.keys()})")
        return f"{of_gpiochips[phandle]}:{pin}:{attr}"


def bin2ulong(filename, single_value=False):
    with open(filename, "rb") as f:
        content = f.read()
    _bytes = []
    for chunk in split_each(content, 4):
        _bytes.append(chunk)
    ulongs = []
    for b in _bytes:
        ulongs.append(int.from_bytes(b, "big"))
    results = [str(u) for u in ulongs]
    if single_value:
        assert len(results) == 1, "Expected single value, got multiple"
        return results[0]
    return results


def get_phandle_map():
    phandle_map = {}
    for entry in os.listdir("/sys/bus/gpio/devices/"):
        if not entry.startswith("gpiochip"):
            continue
        gpiochip = os.path.join("/sys/bus/gpio/devices", entry)
        if not os.path.isfile(os.path.join(gpiochip, "of_node/phandle")):
            continue
        phandle = bin2ulong(os.path.join(gpiochip, "of_node/phandle"), single_value=True)
        chip_num = int(entry[len("gpiochip") :])
        phandle_map[phandle] = chip_num
    return phandle_map


def main():
    if not os.path.isdir("/sys/firmware/devicetree/base/wirenboard/gpios"):
        print("/sys/firmware/devicetree/base/wirenboard/gpios is missing")
        return 0

    of_gpiochips = of_find_gpiochips()
    phandle_map = get_phandle_map()

    node = f"{WB_OF_ROOT}/gpios"
    channels = []
    item_names = []
    input_names = []
    output_names = []
    disconnected_chip_counter = 0

    for gpioname in sorted(of_node_children(node)):
        gpio = of_get_prop_gpio(of_gpiochips, f"{node}/{gpioname}", "io-gpios")
        item_phandle = str(of_get_prop_ulong(f"{node}/{gpioname}", "io-gpios")[0])
        item_chip_num = phandle_map.get(item_phandle)
        if item_chip_num is None:
            disconnected_chip_counter += 1
            gpiochip_path = f"disconnected_gpiochip_{disconnected_chip_counter}"
        else:
            gpiochip_path = f"/dev/gpiochip{item_chip_num}"
        _, pin, inverted = gpio.split(":")
        direction = "input" if of_has_prop(f"{node}/{gpioname}", "input") else "output"
        inverted = inverted == "1"
        initial_state = of_has_prop(f"{node}/{gpioname}", "output-high")
        order = (
            int(of_get_prop_ulong(f"{node}/{gpioname}", "sort-order")[0])
            if of_has_prop(f"{node}/{gpioname}", "sort-order")
            else 0
        )
        channels.append(
            {
                "name": gpioname,
                "gpio": {"chip": gpiochip_path, "offset": int(pin)},
                "direction": direction,
                "inverted": inverted,
                "initial_state": initial_state,
                "order": order,
            }
        )
        if direction == "input":
            input_names.append(gpioname)
        else:
            output_names.append(gpioname)
        item_names.append(gpioname)

    channels = sorted(channels, key=lambda x: x["order"])
    for item in channels:
        del item["order"]

    gpiosysconf = {
        "channels": channels,
    }

    with open(SYS_CONFFILE, "w") as f:
        json.dump(gpiosysconf, f, indent=2)

    # generate confed schema by filtering schema file
    with open(SCHEMA_FILE) as f:
        schema = json.load(f)

    # custom_channels_filter
    schema.get("definitions", {}).get("gpio_channel", {}).get("not", {}).get("properties", {}).get(
        "name", {}
    )["enum"] = item_names
    # undefined_channels_filter
    schema.get("definitions", {}).get("undefined_channel", {}).get("not", {}).get("properties", {}).get(
        "name", {}
    )["enum"] = item_names
    # system_inputs_filter
    schema.get("definitions", {}).get("system_input", {}).get("properties", {}).get("name", {})[
        "enum"
    ] = input_names
    # system_outputs_filter
    schema.get("definitions", {}).get("system_output", {}).get("properties", {}).get("name", {})[
        "enum"
    ] = output_names

    with open(CONFED_SCHEMA_FILE, "w", encoding="utf-8") as f:
        json.dump(schema, f, indent=2, ensure_ascii=False)

    return 0


if __name__ == "__main__":
    sys.exit(main())
