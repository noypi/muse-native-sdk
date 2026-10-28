# hello-app

A [muse-native](https://example.com/muse-native) sample project (TypeScript + TSX).

## Next commands

From the directory containing this README:

```sh
cd hello-app
muse-native build .
muse-native run .
```

## Proxy swap point

All Muse routes in `proxy/proxy.config.json` point at the local stub
endpoint (`http://localhost:8787/stub`). To use the deployed edge proxy,
swap that stub endpoint for your deployed edge proxy URL in
`proxy/proxy.config.json` — no component code change is needed.
