# muse-native-sdk

## Demos

| Demo | Run |
|------|-----|
| `examples/m3-gallery` — gallery of every M3 component/variant | `muse-native run examples/m3-gallery` |
| `examples/flappy` — playable flappy game demo | `muse-native run examples/flappy` |
| `examples/muse-demo` — photo annotator (onboarding → main) | `muse-native run examples/muse-demo` |

Build a demo with `muse-native build <target>` (e.g.
`muse-native build examples/m3-gallery`,
`muse-native build examples/flappy`), or build via
`./scripts/build-demos.sh <target>`. List all demo targets with
`./scripts/build-demos.sh --list`.

## Prerequisites

Requires JDK 17+, Android SDK platform `platforms;android-35`,
NDK `ndk;27.2.12479018`, CMake `3.28.0`, and `muse-native` CLI
version `0.0.0` (see `packages/cli/package.json`).

Install the pinned Android packages:

```sh
sdkmanager install "ndk;27.2.12479018" "cmake;3.28.0" "platform-tools" "platforms;android-35"
```

Verify the toolchain:

```sh
java -version
sdkmanager --list_installed | grep -e "ndk;27.2.12479018" -e "cmake;3.28.0" -e "platforms;android-35"
muse-native --version
```

If `muse-native` is not on PATH, the `muse` alias runs the same CLI:

```sh
muse --version
```

With a JDK installed outside the default location, point at JDK 17+ with:

```sh
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
```
