#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/caps/workaround_db.test.cpp engine/graphics/caps/workaround_db.cc engine/graphics/caps/caps_tier.cc engine/graphics/caps/telemetry.cc -o /tmp/nfr_t5.test
/tmp/nfr_t5.test
