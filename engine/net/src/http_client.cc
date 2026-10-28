// T2 translation unit (FR-NET-004): all HttpClient method definitions live
// in src/transport.cc (single-definition rule). This TU intentionally
// defines nothing further; it includes the shim so the file compiles.
#include "net/http_client.h"
