#!/usr/bin/env python3
import json
from pathlib import Path
import shlex
import sys

commands = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))

# The old ESP32-S2 QMK fork is compiled outside ESP-IDF's CMake graph.
# Reuse the full ESP-IDF compile database so QMK sees every public/private
# component include directory required by its ESP32/TinyUSB platform layer.
selected = []
seen = set()

def add(token):
    if token not in seen:
        seen.add(token)
        selected.append(token)

for entry in commands:
    command = entry.get("command")
    args = entry.get("arguments")
    parts = shlex.split(command) if command else list(args or [])

    i = 0
    while i < len(parts):
        token = parts[i]
        if token.startswith("-I") and len(token) > 2:
            add(token)
        elif token == "-I" and i + 1 < len(parts):
            add("-I" + parts[i + 1])
            i += 1
        elif token == "-isystem" and i + 1 < len(parts):
            add("-isystem")
            add(parts[i + 1])
            i += 1
        i += 1

# TinyUSB's FreeRTOS OSAL includes these headers without the "freertos/" prefix.
idf_path = Path("/opt/esp/idf")
for extra in (
    idf_path / "components/freertos/include/freertos",
    idf_path / "components/fatfs/vfs",
    idf_path / "components/fatfs/src",
    idf_path / "components/wear_levelling/include",
    idf_path / "components/spi_flash/include",
):
    add("-I" + str(extra))

add("-DESP_PLATFORM")
print(" ".join(shlex.quote(x) for x in selected))
