#!/usr/bin/env bash
# MideaMatter — ESP-IDF + ESP-Matter build environment.
# Source this file from the project root: . ./env.sh

if [ -z "${IDF_PATH:-}" ]; then
  midea_idf_export="${ESP_IDF_EXPORT:-$HOME/.espressif/v6.0.2/esp-idf/export.sh}"
  if [ ! -f "$midea_idf_export" ]; then
    echo "ESP-IDF export.sh was not found at: $midea_idf_export" >&2
    echo "Set ESP_IDF_EXPORT to the full path of your ESP-IDF export.sh, then source env.sh again." >&2
    return 1 2>/dev/null || exit 1
  fi
  . "$midea_idf_export"
fi

if [ -z "${ESP_MATTER_PATH:-}" ]; then
  export ESP_MATTER_PATH="$HOME/esp/esp-matter"
fi

if [ ! -f "$ESP_MATTER_PATH/export.sh" ]; then
  echo "ESP-Matter export.sh was not found at: $ESP_MATTER_PATH/export.sh" >&2
  echo "Set ESP_MATTER_PATH to your esp-matter checkout, then source env.sh again." >&2
  return 1 2>/dev/null || exit 1
fi

. "$ESP_MATTER_PATH/export.sh"

# `idf.py set-target` clears generated build configuration. Run it only for a
# fresh checkout or if this folder was previously configured for another chip;
# ordinary `. ./env.sh` followed by `idf.py build` should stay incremental.
if [ ! -f sdkconfig ] || ! grep -q '^CONFIG_IDF_TARGET="esp32c6"$' sdkconfig; then
  idf.py set-target esp32c6
fi

echo "MideaMatter ESP32-C6 environment ready."
echo "  idf.py build   — compile"
echo "  idf.py flash   — write to device"
