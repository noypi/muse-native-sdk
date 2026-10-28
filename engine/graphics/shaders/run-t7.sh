#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/shaders/spirv_only.test.cpp engine/graphics/shaders/shader_bundle.cc engine/graphics/pipeline-cache/prewarm.cc -o /tmp/nfr_t7.test
/tmp/nfr_t7.test
