# egl-probe build (FR-GRAPHICS-011, AC-031).
# Targets: all | host | android [ABIS=...] | clean
# - `host` builds the hermetic host binary `egl-probe-host` with the host
#   g++ (no EGL headers/link needed; EGL resolved via dlopen at runtime).
#   run-efail.sh execs exactly `./egl-probe-host` from this directory.
# - `android` cross-compiles the T1 sources for all four ABI slots
#   (armeabi-v7a, arm64-v8a, x86, x86_64) using the repo's existing NDK
#   toolchain variables (ANDROID_NDK_HOME / ANDROID_NDK_ROOT / ANDROID_NDK,
#   pinned rev 27.2.12479018, ANDROID_PLATFORM android-29 — mirrors the
#   root Makefile engine-android gate, FR-PACKAGING-007).
# - `all` (default) builds host + all four ABI artifacts.
# All builds run in the worktree, never the workspace.

EXPECTED_NDK_REV = 27.2.12479018
ABIS ?= arm64-v8a armeabi-v7a x86_64 x86
ANDROID_PLATFORM_NUM = 29
ANDROID_PLATFORM = android-29

SRC = egl_probe.cc egl_probe_main.cc
HOST_OUT = egl-probe-host
ANDROID_OUT_PREFIX = build/egl-probe-

CXX_STD = -std=c++17
WARN = -Wall -Wextra -Werror
INCLUDES = -I ..

.PHONY: all host android clean

all: host android

host:
	g++ $(CXX_STD) $(WARN) -O2 $(INCLUDES) $(SRC) -o $(HOST_OUT) -ldl

android:
	if [ -z "$${ANDROID_NDK_HOME:-}" ] && [ -z "$${ANDROID_NDK_ROOT:-}" ] && [ -z "$${ANDROID_NDK:-}" ]; then echo "error: NDK not found: set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT/ANDROID_NDK) to the NDK path; expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; if [ ! -d "$$NDK_HOME" ]; then echo "error: NDK path not found: '$$NDK_HOME' (ANDROID_NDK_HOME); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; if [ ! -f "$$NDK_HOME/source.properties" ]; then echo "error: NDK source.properties missing in '$$NDK_HOME' (ANDROID_NDK_HOME); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; INSTALLED_REV=`grep '^Pkg.Revision' "$$NDK_HOME/source.properties" | sed 's/.*= *//'`; if [ "$$INSTALLED_REV" != "$(EXPECTED_NDK_REV)" ]; then echo "error: wrong NDK revision '$$INSTALLED_REV' in '$$NDK_HOME/source.properties' (Pkg.Revision mismatch); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	for ABI in $(ABIS); do case " $$ABI " in *" arm64-v8a "*|*" armeabi-v7a "*|*" x86_64 "*|*" x86 "*) ;; *) echo "error: misconfigured ABI dir/slot '$$ABI' (ABIS='$(ABIS)'); supported slots: arm64-v8a armeabi-v7a x86_64 x86; expected NDK rev $(EXPECTED_NDK_REV)" >&2; exit 1;; esac; done
	mkdir -p build
	for ABI in $(ABIS); do \
	  NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; \
	  CLANG="$$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"; \
	  case "$$ABI" in \
	    arm64-v8a) TARGET="aarch64-linux-android$(ANDROID_PLATFORM_NUM)";; \
	    armeabi-v7a) TARGET="armv7a-linux-androideabi$(ANDROID_PLATFORM_NUM)";; \
	    x86_64) TARGET="x86_64-linux-android$(ANDROID_PLATFORM_NUM)";; \
	    x86) TARGET="i686-linux-android$(ANDROID_PLATFORM_NUM)";; \
	  esac; \
	  if [ ! -x "$$CLANG" ]; then echo "error: NDK clang++ missing at '$$CLANG' (ANDROID_NDK_HOME); expected NDK rev $(EXPECTED_NDK_REV)" >&2; exit 1; fi; \
	  "$$CLANG" --target="$$TARGET" $(CXX_STD) $(WARN) -O2 $(INCLUDES) $(SRC) -o "$(ANDROID_OUT_PREFIX)$$ABI" -ldl -llog; \
	  if [ ! -f "$(ANDROID_OUT_PREFIX)$$ABI" ]; then echo "error: missing output egl-probe binary for ABI slot $$ABI at '$(ANDROID_OUT_PREFIX)$$ABI'" >&2; exit 1; fi; \
	done

clean:
	rm -f $(HOST_OUT) build/egl-probe-arm64-v8a build/egl-probe-armeabi-v7a build/egl-probe-x86_64 build/egl-probe-x86
