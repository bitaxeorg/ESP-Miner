#!/usr/bin/env python3
"""Check that the shared firmware configuration leaves Bonanza's UART pins free."""

import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdkconfig", type=Path, nargs="?", default=Path("build/config/sdkconfig.json"))
    args = parser.parse_args()
    config = json.loads(args.sdkconfig.read_text())
    expected = {
        "ESP_CONSOLE_USB_SERIAL_JTAG": True,
        "ESP_CONSOLE_SECONDARY_NONE": True,
        "ESP_CONSOLE_UART_DEFAULT": False,
        "ESP_CONSOLE_UART_CUSTOM": False,
        "ESP_CONSOLE_UART": False,
    }
    errors = [name for name, value in expected.items() if config.get(name, False) is not value]
    if errors:
        print(f"Bonanza console configuration failed: {', '.join(errors)}", file=sys.stderr)
        return 1
    print(f"Bonanza console configuration passed: {args.sdkconfig}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
