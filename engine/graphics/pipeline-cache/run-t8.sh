#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/pipeline-cache/pipeline_cache.test.cpp engine/graphics/pipeline-cache/pipeline_cache.cc -o /tmp/nfr_t8.test
/tmp/nfr_t8.test
