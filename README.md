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
android sdk install "ndk;27.2.12479018" "cmake;3.28.0" "platform-tools" "platforms;android-35"
```

Verify the toolchain:

```sh
java -version
android sdk list | grep -e "ndk;27.2.12479018" -e "cmake;3.28.0" -e "platforms;android-35"
muse-native --version
```

On a read-only SDK mount (`android sdk list` cannot take `.sdk/lock`),
verify via file existence instead (primary on this mount):

```sh
ls $ANDROID_HOME/platforms/android-35
ls $ANDROID_HOME/ndk/27.2.12479018
ls $ANDROID_HOME/cmake/3.28.0
```

If `muse-native` is not on PATH, the `muse` alias runs the same CLI:

```sh
muse --version
```

With a JDK installed outside the default location, point at JDK 17+ with:

```sh
export ANDROID_HOME=/media/saif/EXTRA/sdk/android
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
```

## Deferred behaviours

The following behaviours are Deferred in this sample (not supported here),
each linked to the spec recording the exclusion:

- `.capo` SFC authoring — Deferred; see [specs-tsx-dialect.md](manual-context.local/specs-tsx-dialect.md).
- `.vue` input — Deferred; see [specs-tsx-dialect.md](manual-context.local/specs-tsx-dialect.md).
- `watchAsync` — Deferred; see [specs-muse-core.md](manual-context.local/specs-muse-core.md).
- `watchEffectAsync` — Deferred; see [specs-muse-core.md](manual-context.local/specs-muse-core.md).
- Arbitrary lazy components — Deferred; see [specs-tsx-dialect.md](manual-context.local/specs-tsx-dialect.md).
- Update lifecycle hooks (`onUpdated`/`onBeforeUpdate`) — Deferred; see [specs-muse-core.md](manual-context.local/specs-muse-core.md).
