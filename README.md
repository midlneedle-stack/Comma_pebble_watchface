# Comma Watchface

![Comma Preview](Comma_preview.png)

Comma is a Pebble watchface experiment that renders both the background grid and the digits as smoothly-eased diagonal artifacts. The native C code lives inside `Comma/src/c`, JavaScript helpers (currently only logging) inside `Comma/src/pkjs`, and Pebble build tooling is configured via `Comma/wscript`.

## Building

```sh
cd Comma
pebble build
```

## Deploying

```sh
cd Comma
pebble install --emulator basalt   # or any supported platform
```

The resulting `.pbw` bundle is generated under `Comma/build`.
