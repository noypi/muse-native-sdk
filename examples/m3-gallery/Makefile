# Scaffold Makefile — reproducible APK assemble/sign/verify/install (FR-PACKAGING-008).
# Targets: apk | sign | verify | install | clean (run in examples/m3-gallery).
# Debug key only (no Play release signing).

APP_NAME = m3-gallery
BUILD_DIR = build
STAGING_DIR = $(BUILD_DIR)/staging
LINKED_ZIP = $(BUILD_DIR)/base-linked.zip
UNSIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-unsigned.apk
ALIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-aligned.apk
SIGNED_APK = $(BUILD_DIR)/$(APP_NAME)-debug.apk
DEBUG_KEYSTORE = debug.keystore
ASSET_SRC = dist/tree.mui.bin
ASSET_DST = assets/tree.mui.bin
ANDROID_JAR = $(ANDROID_HOME)/platforms/android-34/android.jar
ENGINE_LIBS = ../../build/android-arm64-v8a/libRuntime.so ../../build/android-armeabi-v7a/libRuntime.so ../../build/android-x86_64/libRuntime.so ../../build/android-x86/libRuntime.so

.PHONY: apk sign verify install clean

apk:
	mkdir -p $(STAGING_DIR) $(BUILD_DIR)/gen $(BUILD_DIR)/classes $(BUILD_DIR)/dex
	aapt2 compile --dir res -o $(BUILD_DIR)/compiled.zip
	aapt2 link -o $(LINKED_ZIP) -I $(ANDROID_JAR) --manifest AndroidManifest.xml --java $(BUILD_DIR)/gen -A assets $(BUILD_DIR)/compiled.zip
	javac -source 17 -target 17 -classpath $(ANDROID_JAR) -d $(BUILD_DIR)/classes $$(find src -name '*.java') $$(find $(BUILD_DIR)/gen -name '*.java')
	d8 --lib $(ANDROID_JAR) --output $(BUILD_DIR)/dex $$(find $(BUILD_DIR)/classes -name '*.class')
	mkdir -p $(STAGING_DIR)/lib/arm64-v8a $(STAGING_DIR)/lib/armeabi-v7a $(STAGING_DIR)/lib/x86_64 $(STAGING_DIR)/lib/x86 $(STAGING_DIR)/assets/tree
	unzip -o $(LINKED_ZIP) AndroidManifest.xml resources.arsc -d $(STAGING_DIR)
	test -f ../../build/android-arm64-v8a/libRuntime.so || { echo "error: missing engine lib for ABI slot arm64-v8a at '../../build/android-arm64-v8a/libRuntime.so'" >&2; exit 1; }
	cp ../../build/android-arm64-v8a/libRuntime.so $(STAGING_DIR)/lib/arm64-v8a/libRuntime.so
	test -f ../../build/android-armeabi-v7a/libRuntime.so || { echo "error: missing engine lib for ABI slot armeabi-v7a at '../../build/android-armeabi-v7a/libRuntime.so'" >&2; exit 1; }
	cp ../../build/android-armeabi-v7a/libRuntime.so $(STAGING_DIR)/lib/armeabi-v7a/libRuntime.so
	test -f ../../build/android-x86_64/libRuntime.so || { echo "error: missing engine lib for ABI slot x86_64 at '../../build/android-x86_64/libRuntime.so'" >&2; exit 1; }
	cp ../../build/android-x86_64/libRuntime.so $(STAGING_DIR)/lib/x86_64/libRuntime.so
	test -f ../../build/android-x86/libRuntime.so || { echo "error: missing engine lib for ABI slot x86 at '../../build/android-x86/libRuntime.so'" >&2; exit 1; }
	cp ../../build/android-x86/libRuntime.so $(STAGING_DIR)/lib/x86/libRuntime.so
	cp $(BUILD_DIR)/dex/classes.dex $(STAGING_DIR)/classes.dex
	cp -r tree/* $(STAGING_DIR)/assets/tree/ 2>/dev/null || cp -r assets/* $(STAGING_DIR)/assets/ 2>/dev/null || true
	cd $(STAGING_DIR) && zip -r ../$(notdir $(UNSIGNED_APK)) AndroidManifest.xml classes.dex resources.arsc lib assets

sign: apk
	zipalign -f 4 $(UNSIGNED_APK) $(ALIGNED_APK)
	apksigner sign --ks $(DEBUG_KEYSTORE) --ks-pass pass:android --key-pass pass:android --out $(SIGNED_APK) $(ALIGNED_APK)
	@echo "signed debug build: $(SIGNED_APK) (debug key only)"

verify: sign
	apksigner verify --verbose $(SIGNED_APK)
	for entry in AndroidManifest.xml classes.dex resources.arsc; do if ! unzip -l $(SIGNED_APK) | grep -q "$$entry"; then echo "error: verify failed: APK entry missing: $$entry in $(SIGNED_APK)" >&2; exit 1; fi; done
	if ! apksigner verify $(SIGNED_APK) >/dev/null 2>&1; then echo "error: verify failed: apksigner verify rejected $(SIGNED_APK)" >&2; exit 1; fi

install: sign
	mkdir -p $(dir $(ASSET_DST))
	test -f $(ASSET_SRC) || { echo "error: install failed: missing build output at $(ASSET_SRC); run muse-native build first" >&2; exit 1; }
	cp $(ASSET_SRC) $(ASSET_DST)
	if ! adb devices | awk 'NR>1 && $$2=="device" { found=1 } END { exit !found }'; then echo "error: install failed: no device connected (adb devices shows no device); connect a device with USB debugging instead of partial-installing" >&2; exit 1; fi
	adb install -r $(SIGNED_APK)

clean:
	rm -rf $(BUILD_DIR) dist *.apk
