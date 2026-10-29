# Root Makefile — reproducible engine host/Android builds (FR-PACKAGING-007).
# Targets: engine-host | engine-android [ABIS=...] | clean
# NDK pin (NFR-BUILD-016, r27): 27.2.12479018.

EXPECTED_NDK_REV = 27.2.12479018
ABIS ?= arm64-v8a armeabi-v7a x86_64 x86
BUILD_DIR = build
HOST_BUILD_DIR = $(BUILD_DIR)/host
ANDROID_BUILD_ROOT = $(BUILD_DIR)/android
ANDROID_PLATFORM = android-29

engine-host:
	cmake -S engine/runtime -B $(HOST_BUILD_DIR)
	cmake --build $(HOST_BUILD_DIR)
	ctest --test-dir $(HOST_BUILD_DIR)

engine-android:
	if [ -z "$${ANDROID_NDK_HOME:-}" ] && [ -z "$${ANDROID_NDK_ROOT:-}" ] && [ -z "$${ANDROID_NDK:-}" ]; then echo "error: NDK not found: set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT/ANDROID_NDK) to the NDK path; expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; if [ ! -d "$$NDK_HOME" ]; then echo "error: NDK path not found: '$$NDK_HOME' (ANDROID_NDK_HOME); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; if [ ! -f "$$NDK_HOME/source.properties" ]; then echo "error: NDK source.properties missing in '$$NDK_HOME' (ANDROID_NDK_HOME); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	NDK_HOME="$${ANDROID_NDK_HOME:-$${ANDROID_NDK_ROOT:-$${ANDROID_NDK}}}"; INSTALLED_REV=`grep '^Pkg.Revision' "$$NDK_HOME/source.properties" | sed 's/.*= *//'`; if [ "$$INSTALLED_REV" != "$(EXPECTED_NDK_REV)" ]; then echo "error: wrong NDK revision '$$INSTALLED_REV' in '$$NDK_HOME/source.properties' (Pkg.Revision mismatch); expected NDK rev 27.2.12479018" >&2; exit 1; fi
	for ABI in $(ABIS); do case " $$ABI " in *" arm64-v8a "*|*" armeabi-v7a "*|*" x86_64 "*|*" x86 "*) ;; *) echo "error: misconfigured ABI dir/slot '$$ABI' (ABIS='$(ABIS)'); supported slots: arm64-v8a armeabi-v7a x86_64 x86; expected NDK rev $(EXPECTED_NDK_REV)" >&2; exit 1;; esac; done
	for ABI in $(ABIS); do case "$$ABI" in arm64-v8a) EXPECTED_MACHINE="aarch64";; armeabi-v7a) EXPECTED_MACHINE="ARM";; x86_64) EXPECTED_MACHINE="x86-64";; x86) EXPECTED_MACHINE="80386";; *) echo "error: misconfigured ABI dir/slot '$$ABI' (ABIS='$(ABIS)'); supported slots: arm64-v8a armeabi-v7a x86_64 x86; expected NDK rev $(EXPECTED_NDK_REV)" >&2; exit 1;; esac; OUTDIR="$(ANDROID_BUILD_ROOT)-$$ABI"; cmake -S engine/runtime -B "$$OUTDIR" -DCMAKE_TOOLCHAIN_FILE="$$NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$$ABI" -DANDROID_PLATFORM="$(ANDROID_PLATFORM)"; cmake --build "$$OUTDIR"; SO="$$OUTDIR/libRuntime.so"; if [ ! -f "$$SO" ]; then echo "error: missing output libRuntime.so for ABI slot $$ABI at '$$SO'" >&2; exit 1; fi; INFO="`file -b "$$SO" 2>/dev/null` `readelf -h "$$SO" 2>/dev/null`"; case "$$INFO" in *$$EXPECTED_MACHINE*) ;; *) echo "error: arch mismatch for ABI slot $$ABI: expected ELF machine $$EXPECTED_MACHINE (aarch64 for arm64-v8a), got: $$INFO" >&2; exit 1;; esac; done

clean:
	rm -rf $(HOST_BUILD_DIR) $(BUILD_DIR)/android-arm64-v8a $(BUILD_DIR)/android-armeabi-v7a $(BUILD_DIR)/android-x86_64 $(BUILD_DIR)/android-x86 build
