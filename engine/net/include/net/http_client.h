#pragma once

// T2 re-export shim (FR-NET-004): the single HttpClient definition lives in
// net/transport.h (T1 TU owns all method definitions in src/transport.cc).
// This header exists so the shared contract test can include
// net/http_client.h directly; it defines nothing itself.
#include "net/transport.h"
