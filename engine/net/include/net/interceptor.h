#pragma once

// T2 re-export shim (FR-NET-004): Interceptor / Next / BodyChunk are defined
// once in net/types.h (via net/transport.h). This header exists so the
// shared contract test can include net/interceptor.h directly; it defines
// nothing itself.
#include "net/transport.h"
