# Scaffold Makefile — reproducible APK assemble/sign/verify/install (FR-PACKAGING-008).
# Targets: apk | sign | verify | install | clean (run in examples/m3-gallery).
# Debug key only (no Play release signing).

ifndef MUSE_NATIVE_HOME
$(error MUSE_NATIVE_HOME is not set. Example: export MUSE_NATIVE_HOME=<sdk-install>/build)
endif

APP_NAME = m3-gallery
BUILD_DIR = build
STAGING_DIR = $(BUILD_DIR)/staging
LINKED_ZIP = $(BUILD_DIR)/base-linked.zip
UNSIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-unsigned.apk
ALIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-aligned.apk
SIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-debug.apk
DEBUG_KEYSTORE = ~/.android/debug.keystore
ASSET_SRC = dist/tree.mui.bin
ASSET_DST = assets/tree.mui.bin
ANDROID_JAR = $(ANDROID_PLATFORM)/android.jar
ENGINE_LIBS = $(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so $(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so $(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so $(MUSE_NATIVE_HOME)/android-x86/libRuntime.so

.PHONY: apk sign verify install clean parity assert-no-diagram

# T2 (FR-M3-011 AC-M3-025; RULE-M3-007, RULE-GRAPHICS-003): mock-painter
# reachability gate, scanned per gallery page target. Any page target that
# reaches a mock-painter symbol (MockPainter, the PaintCaption box/caption
# stand-in, any other mock diagram painter) fails the build with the symbol
# named. Oracle diagram painters remain importable only from unit-test /
# hermetic targets (test/, tests/, fixtures) — never from src/ page targets
# or device frames.
# TC-M3-026 check: `muse-native gallery-real --assert-no-diagram` (equivalently
# `node ../../packages/cli/lib/gallery-real.js --assert-no-diagram`) passes on
# a clean tree and fails the build naming the symbol otherwise.
assert-no-diagram:
	node ../../packages/cli/lib/gallery-real.js --assert-no-diagram --page gallery-page-button
	@if grep -rn --include='*.tsx' --include='*.js' -E 'MockPainter|PaintCaption' src/ 2>/dev/null; then echo "error: mock-painter reachable: MockPainter/PaintCaption stand-in found in src/ page targets (see hits above) — oracle diagram painters are unit-test/hermetic-only" >&2; exit 1; fi

# TC-GRAPHICS-041 (AC-GRAPHICS-039 T4): cpu-vs-skia pixel parity via the AC-035
# harness shape — same gallery tree on both paths, md5 compare. Exits 0 iff
# md5(cpu_frame) == md5(skia_frame); prints both md5s either way.
parity:
	muse-native gallery-parity --cpu-vs-skia --assert-md5

apk: assert-no-diagram
	@if [ -z "$(MUSE_NATIVE_HOME)" ]; then echo "error: MUSE_NATIVE_HOME is not set. Example: export MUSE_NATIVE_HOME=<sdk-install>/build" >&2; exit 1; fi
	@test -f "$(MUSE_NATIVE_HOME)/VERSION" || { echo "error: missing SDK VERSION file at '$(MUSE_NATIVE_HOME)/VERSION'" >&2; exit 1; }
	@if [ "$$(awk 'END{print NR}' "$(MUSE_NATIVE_HOME)/VERSION")" -ne 3 ]; then echo "error: malformed SDK VERSION at '$(MUSE_NATIVE_HOME)/VERSION': expected 3 lines, found $$(awk 'END{print NR}' "$(MUSE_NATIVE_HOME)/VERSION")" >&2; exit 1; fi
	@if ! sed -n '1p' "$(MUSE_NATIVE_HOME)/VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$'; then echo "error: malformed SDK VERSION at '$(MUSE_NATIVE_HOME)/VERSION': line 1 must be SemVer MAJOR.MINOR.PATCH, found '$$(sed -n '1p' "$(MUSE_NATIVE_HOME)/VERSION")'" >&2; exit 1; fi
	@if ! sed -n '2p' "$(MUSE_NATIVE_HOME)/VERSION" | grep -Eq '^sha=[0-9a-f]+ date=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$$'; then echo "error: malformed SDK VERSION at '$(MUSE_NATIVE_HOME)/VERSION': line 2 must be 'sha=<hex> date=<ISO-8601>', found '$$(sed -n '2p' "$(MUSE_NATIVE_HOME)/VERSION")'" >&2; exit 1; fi
	@if [ "$$(sed -n '3p' "$(MUSE_NATIVE_HOME)/VERSION")" != "abis=arm64-v8a armeabi-v7a x86_64 x86" ]; then echo "error: malformed SDK VERSION at '$(MUSE_NATIVE_HOME)/VERSION': line 3 must be 'abis=arm64-v8a armeabi-v7a x86_64 x86', found '$$(sed -n '3p' "$(MUSE_NATIVE_HOME)/VERSION")'" >&2; exit 1; fi
	@sdk_pin="$$(grep -E '^[[:space:]]*sdk:' muse-native.yaml 2>/dev/null | head -n 1 | sed 's/^[[:space:]]*sdk:[[:space:]]*//;s/[[:space:]]*$$//')"; \
	sdk_ver="$$(sed -n '1p' "$(MUSE_NATIVE_HOME)/VERSION")"; \
	if [ -z "$$sdk_pin" ]; then echo "warning: no 'sdk:' pin in muse-native.yaml; building against SDK version $$sdk_ver" >&2; \
	elif [ "$$sdk_pin" != "$$sdk_ver" ]; then echo "error: SDK version mismatch: expected $$sdk_ver (from '$(MUSE_NATIVE_HOME)/VERSION' line 1), found sdk: $$sdk_pin (from muse-native.yaml)" >&2; exit 1; fi
	test -s "$(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so" || { echo "error: missing engine lib for ABI slot arm64-v8a at '$(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so'" >&2; exit 1; }
	test -s "$(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so" || { echo "error: missing engine lib for ABI slot armeabi-v7a at '$(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so'" >&2; exit 1; }
	test -s "$(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so" || { echo "error: missing engine lib for ABI slot x86_64 at '$(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so'" >&2; exit 1; }
	test -s "$(MUSE_NATIVE_HOME)/android-x86/libRuntime.so" || { echo "error: missing engine lib for ABI slot x86 at '$(MUSE_NATIVE_HOME)/android-x86/libRuntime.so'" >&2; exit 1; }
	@if ! grep -a -q "Java_com_muse_host_MainActivity_nativeSetImeBottom" "$(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so"; then echo "error: stale-lib: arm64-v8a '$(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so' missing required symbol Java_com_muse_host_MainActivity_nativeSetImeBottom (expected fresh engine output engine/runtime/build-android-arm64-v8a/libRuntime.so) — reinstall the SDK" >&2; exit 1; fi
	@if ! grep -a -q "Java_com_muse_host_MainActivity_nativeSetImeBottom" "$(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so"; then echo "error: stale-lib: armeabi-v7a '$(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so' missing required symbol Java_com_muse_host_MainActivity_nativeSetImeBottom (expected fresh engine output engine/runtime/build-android-armeabi-v7a/libRuntime.so) — reinstall the SDK" >&2; exit 1; fi
	@if ! grep -a -q "Java_com_muse_host_MainActivity_nativeSetImeBottom" "$(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so"; then echo "error: stale-lib: x86_64 '$(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so' missing required symbol Java_com_muse_host_MainActivity_nativeSetImeBottom (expected fresh engine output engine/runtime/build-android-x86_64/libRuntime.so) — reinstall the SDK" >&2; exit 1; fi
	@if ! grep -a -q "Java_com_muse_host_MainActivity_nativeSetImeBottom" "$(MUSE_NATIVE_HOME)/android-x86/libRuntime.so"; then echo "error: stale-lib: x86 '$(MUSE_NATIVE_HOME)/android-x86/libRuntime.so' missing required symbol Java_com_muse_host_MainActivity_nativeSetImeBottom (expected fresh engine output engine/runtime/build-android-x86/libRuntime.so) — reinstall the SDK" >&2; exit 1; fi
	rm -f $(UNSIGNED_APK)
	muse-native build .
	mkdir -p assets && cp dist/tree.mui.bin assets/tree.mui.bin
	@built_hash="$$(sha256sum dist/tree.mui.bin 2>/dev/null | cut -d' ' -f1)"; \
	staged_hash="$$(sha256sum assets/tree.mui.bin 2>/dev/null | cut -d' ' -f1)"; \
	if [ -z "$$built_hash" ] || [ -z "$$staged_hash" ]; then echo "error: stale-asset: assets/tree.mui.bin vs dist/tree.mui.bin: missing tree bytes (built='$$built_hash' staged='$$staged_hash') — rebuild required" >&2; exit 1; fi; \
	if [ "$$built_hash" != "$$staged_hash" ]; then echo "error: stale-asset: assets/tree.mui.bin baked $$staged_hash != built dist/tree.mui.bin $$built_hash — rebuild required" >&2; exit 1; fi
	mkdir -p $(STAGING_DIR) $(BUILD_DIR)/gen $(BUILD_DIR)/classes $(BUILD_DIR)/dex
	aapt2 compile --dir res -o $(BUILD_DIR)/compiled.zip
	aapt2 link -o $(LINKED_ZIP) -I $(ANDROID_JAR) --manifest AndroidManifest.xml --java $(BUILD_DIR)/gen -A assets $(BUILD_DIR)/compiled.zip
	javac -source 17 -target 17 -classpath $(ANDROID_JAR) -d $(BUILD_DIR)/classes $$(find src -name '*.java') $$(find $(BUILD_DIR)/gen -name '*.java')
	d8 --lib $(ANDROID_JAR) --output $(BUILD_DIR)/dex $$(find $(BUILD_DIR)/classes -name '*.class')
	mkdir -p $(STAGING_DIR)/lib/arm64-v8a $(STAGING_DIR)/lib/armeabi-v7a $(STAGING_DIR)/lib/x86_64 $(STAGING_DIR)/lib/x86 $(STAGING_DIR)/assets/tree
	unzip -o $(LINKED_ZIP) AndroidManifest.xml resources.arsc -d $(STAGING_DIR)
	cp "$(MUSE_NATIVE_HOME)/android-arm64-v8a/libRuntime.so" $(STAGING_DIR)/lib/arm64-v8a/libRuntime.so
	cp "$(MUSE_NATIVE_HOME)/android-armeabi-v7a/libRuntime.so" $(STAGING_DIR)/lib/armeabi-v7a/libRuntime.so
	cp "$(MUSE_NATIVE_HOME)/android-x86_64/libRuntime.so" $(STAGING_DIR)/lib/x86_64/libRuntime.so
	cp "$(MUSE_NATIVE_HOME)/android-x86/libRuntime.so" $(STAGING_DIR)/lib/x86/libRuntime.so
	cp $(BUILD_DIR)/dex/classes.dex $(STAGING_DIR)/classes.dex
	cp -r tree/* $(STAGING_DIR)/assets/tree/ 2>/dev/null || cp -r assets/* $(STAGING_DIR)/assets/ 2>/dev/null || true
	# NOTE (T2, AC-023): next line's parent-dir ref stays inside BUILD_DIR staging; exempt from no-traversal rule.
	cd $(STAGING_DIR) && zip -r ../$(notdir $(UNSIGNED_APK)) AndroidManifest.xml classes.dex lib assets && zip -g -0 ../$(notdir $(UNSIGNED_APK)) resources.arsc

sign: apk
	zipalign -f 4 $(UNSIGNED_APK) $(ALIGNED_APK)
	apksigner sign --ks $(DEBUG_KEYSTORE) --ks-pass pass:android --key-pass pass:android --out $(SIGNED_APK) $(ALIGNED_APK)
	@echo "signed debug build: $(SIGNED_APK) (debug key only)"

verify: sign
	apksigner verify --verbose $(SIGNED_APK)
	for entry in AndroidManifest.xml classes.dex resources.arsc; do if ! unzip -l $(SIGNED_APK) | grep -q "$$entry"; then echo "error: verify failed: APK entry missing: $$entry in $(SIGNED_APK)" >&2; exit 1; fi; done
	if ! unzip -lv $(SIGNED_APK) | grep "resources.arsc" | grep -q "Stored"; then echo "error: verify failed: resources.arsc must be Stored (method 0) + 4-byte aligned in $(SIGNED_APK)" >&2; exit 1; fi
	if command -v zipalign >/dev/null 2>&1; then if ! zipalign -c 4 $(SIGNED_APK) >/dev/null 2>&1; then echo "error: verify failed: resources.arsc not Stored+aligned (zipalign -c 4 rejected $(SIGNED_APK))" >&2; exit 1; fi; fi
	if ! apksigner verify $(SIGNED_APK) >/dev/null 2>&1; then echo "error: verify failed: apksigner verify rejected $(SIGNED_APK)" >&2; exit 1; fi
	# JNI contract gate (BUG-TEXT-004 / AC-RUNTIME-027): every native* declared
	# in MainActivity.java must be a `T` dynamic export in each staged ABI lib.
	# APK-inner check is best-effort (skipped with a warning when unzip is absent).
	GATE=../../engine/runtime/tests/jni_contract_test.sh; JAVA_SRC=src/MainActivity.java; BRIDGE=../../engine/runtime/src/jni_bridge.cpp; \
	for abi in arm64-v8a armeabi-v7a x86_64 x86; do \
	  SO="$(STAGING_DIR)/lib/$$abi/libRuntime.so"; \
	  if [ ! -s "$$SO" ]; then echo "error: verify failed: missing staged ABI lib: $$SO" >&2; exit 1; fi; \
	  sh "$$GATE" "$$SO" "$$JAVA_SRC" "$$BRIDGE" || exit 1; \
	done; \
	if command -v unzip >/dev/null 2>&1; then \
	  TMPD="$$(mktemp -d)"; trap 'rm -rf "$$TMPD"' EXIT INT TERM; \
	  for abi in arm64-v8a armeabi-v7a x86_64 x86; do \
	    if unzip -l $(SIGNED_APK) | grep -q "lib/$$abi/libRuntime.so"; then \
	      mkdir -p "$$TMPD/$$abi"; unzip -o -j $(SIGNED_APK) "lib/$$abi/libRuntime.so" -d "$$TMPD/$$abi" >/dev/null; \
	      sh "$$GATE" "$$TMPD/$$abi/libRuntime.so" "$$JAVA_SRC" "$$BRIDGE" || exit 1; \
	    else echo "warning: verify: APK has no lib/$$abi/libRuntime.so entry; skipping APK-inner check for $$abi" >&2; fi; \
	  done; \
	  rm -rf "$$TMPD"; trap - EXIT INT TERM; \
	else echo "warning: verify: unzip not found; skipping APK-inner JNI check (staged libs already gated)" >&2; fi

install: sign
	mkdir -p $(dir $(ASSET_DST))
	test -f $(ASSET_SRC) || { echo "error: install failed: missing build output at $(ASSET_SRC); run muse-native build first" >&2; exit 1; }
	cp $(ASSET_SRC) $(ASSET_DST)
	if ! adb devices | awk 'NR>1 && $$2=="device" { found=1 } END { exit !found }'; then echo "error: install failed: no device connected (adb devices shows no device); connect a device with USB debugging instead of partial-installing" >&2; exit 1; fi
	adb install -r $(SIGNED_APK)

clean:
	rm -rf $(BUILD_DIR) dist *.apk
