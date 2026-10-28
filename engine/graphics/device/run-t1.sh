#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/device/backend_selection.test.cpp engine/graphics/device/backend_probe.cc engine/graphics/device/backend_selection.cc -o /tmp/nfr_t1.test
/tmp/nfr_t1.test
