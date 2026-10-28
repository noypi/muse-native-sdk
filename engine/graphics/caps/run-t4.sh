#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/caps/caps_telemetry.test.cpp engine/graphics/caps/caps_tier.cc engine/graphics/caps/telemetry.cc -o /tmp/nfr_t4.test
/tmp/nfr_t4.test
