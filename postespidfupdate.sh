#!/bin/bash
set -eu
. /opt/esp-idf/export.sh
idf.py fullclean
rm -v sdkconfig
idf.py set-target esp32-s3