# Generated keep rules — AC-BUILD-012 (generator-emitted, never hand-written)
# generator: r8-keepgen/1.0 (codegen surface: JNI + FlatBuffers + JSI + Hermes)
# provenance: emitted by the keep-rule generator; do not edit by hand.
# Fixture for toolchain/r8/keepRules.test.js (T8).

# native-method: keep annotated native entry points (JNI/codegen surface)
-keepclasseswithmembernames class * {
  native <methods>; # native-method
}

# jni-callback: keep methods invoked back from native (JNI callbacks)
-keep class * {
  void jni-callback*(...); # jni-callback
}

# flatbuffer: keep FlatBuffers generated types and fields
-keep class * extends com.google.flatbuffers.Table { *; } # flatbuffer

# jsi-exposed: keep JSI-exposed host bindings
-keep class com.muse.jsi.** { *; } # jsi-exposed

# hermes-reflection: keep Hermes reflection entry points
-keep class com.facebook.hermes.** { *; } # hermes-reflection
