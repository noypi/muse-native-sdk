# Troubleshooting

## 1. Unauthorized device

**Symptom:** `adb devices` lists the device as `unauthorized`, and
`muse-native run` refuses to install because no device is available.

**Fix:** On the device, accept the USB-debugging authorization prompt, then
re-check the device list:

```sh
adb devices
muse-native run examples/m3-gallery
```

The `muse` alias runs the same CLI:

```sh
muse run examples/m3-gallery
```

## 2. Emulator IME limitations

**Symptom:** On an emulator, the on-screen keyboard (IME) does not appear or
does not deliver keystrokes, so text fields in the sample cannot be exercised.

**Fix:** Enable the on-screen keyboard in the emulator settings, cold-boot the
emulator, then rerun the sample:

```sh
adb reboot
adb devices
muse-native run examples/muse-demo
```

The `muse` alias runs the same CLI:

```sh
muse run examples/muse-demo
```

## 3. Missing or mismatched NDK path

**Symptom:** The build fails because `ANDROID_NDK_HOME` is unset or points at
the wrong NDK revision (expected `27.2.12479018`).

**Fix:** Install the pinned Android packages:

```sh
sdkmanager install "ndk;27.2.12479018" "cmake;3.28.0" "platform-tools" "platforms;android-35"
```

Then point the environment at the SDK and the pinned NDK, plus JDK 17+:

```sh
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.2.12479018
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
```

Verify the toolchain:

```sh
sdkmanager --list_installed | grep -e "ndk;27.2.12479018" -e "cmake;3.28.0" -e "platforms;android-35"
java -version
muse-native --version
muse --version
```

## 4. Stale build cache

**Symptom:** A stale build cache is reused: edits do not appear in the rebuilt
sample, or the build keeps failing after the toolchain was fixed.

**Fix:** Clear the sample build output and rebuild from clean:

```sh
rm -rf build
muse-native build examples/m3-gallery
```

The `muse` alias runs the same CLI:

```sh
muse build examples/m3-gallery
```
