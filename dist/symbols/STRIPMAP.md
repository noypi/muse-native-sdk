# strip map — stripped -> unstripped

This is the committed layout placeholder documenting the
stripped→unstripped mapping format that real builds populate.

Each shipped stripped artifact under `lib/<abi>/libmuse.so` must have one
mapping line pointing at its retained unstripped counterpart under
`symbols/unstripped/<abi>/libmuse.so`:

```
<stripped> -> <unstripped>
```

Example (one line per shipped .so):

```
lib/arm64-v8a/libmuse.so -> symbols/unstripped/arm64-v8a/libmuse.so
```
