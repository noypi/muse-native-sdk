# Scaffold Makefile — reproducible APK assemble/sign/verify/install (FR-PACKAGING-008).
# Targets: apk | sign | verify | install | clean (run in examples/m3-gallery).
# Debug key only (no Play release signing).

ifndef MUSE_NATIVE_HOME
$(error MUSE_NATIVE_HOME is not set. Example: export MUSE_NATIVE_HOME=<sdk-install>/build)
endif

APP_NAME = hello-app
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

.PHONY: apk sign verify install clean

apk:
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
	rm -f $(UNSIGNED_APK)
	muse-native build .
	mkdir -p assets && cp dist/tree.mui.bin assets/tree.mui.bin
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

install: sign
	mkdir -p $(dir $(ASSET_DST))
	test -f $(ASSET_SRC) || { echo "error: install failed: missing build output at $(ASSET_SRC); run muse-native build first" >&2; exit 1; }
	cp $(ASSET_SRC) $(ASSET_DST)
	if ! adb devices | awk 'NR>1 && $$2=="device" { found=1 } END { exit !found }'; then echo "error: install failed: no device connected (adb devices shows no device); connect a device with USB debugging instead of partial-installing" >&2; exit 1; fi
	adb install -r $(SIGNED_APK)

clean:
	rm -rf $(BUILD_DIR) dist *.apk
