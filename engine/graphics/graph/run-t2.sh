#!/bin/sh
set -e
g++ -std=c++17 -I engine/graphics engine/graphics/graph/backend_agnostic.test.cpp engine/graphics/graph/render_graph.cc engine/graphics/graph/backend_dispatch.cc -o /tmp/nfr_t2.test
/tmp/nfr_t2.test
