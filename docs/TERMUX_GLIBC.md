# Building vkBasalt for a glibc userspace on Android

This is for `termux-glibc` / `glibc-runner` / a proot distribution, where the
whole userspace (Vulkan loader, drivers, the application) is glibc based and
lives under a prefix such as `/data/data/com.termux/files/usr` or `/usr`.

It is **not** needed for the bionic Termux packages.

## Requirements

| Package | Why |
|---|---|
| `meson`, `ninja` | build system |
| `glslang` | `glslangValidator` compiles the `.glsl` effects into headers at build time |
| `spirv-headers` | `src/reshade/spirv.hpp` includes `spirv/unified1/spirv.hpp`; the build fails without it |
| `vulkan-loader` / `libvulkan` | only needed at runtime |
| `libx11` (optional) | only for the toggle hotkey, see below |

On a glibc prefix these come from the distribution package manager, for example
`pacman -S meson ninja glslang spirv-headers vulkan-icd-loader libx11` or
`apt install meson ninja-build glslang-tools spirv-headers libvulkan-dev libx11-dev`.

## Build

```sh
./scripts/build-termux-glibc.sh
```

It auto-detects the prefix, whether `libx11` is available, and where
`glslangValidator` lives. Override any of them:

```sh
VKBASALT_PREFIX=/usr BUILD_DIR=build-termux JOBS=8 ./scripts/build-termux-glibc.sh
```

Manual equivalent:

```sh
meson setup build --prefix="$PREFIX" --libdir=lib --buildtype=release \
      -Dappend_libdir_vkbasalt=true -Dx11=false
ninja -C build && ninja -C build install
```

## The X11 keyboard backend

`x11` (default on) only provides the toggle hotkey (`toggleKey`, default
`Home`). Inside a proot there is frequently no `libX11` for the glibc prefix, so
the script turns it off automatically when `pkg-config --exists x11` fails.

With `-Dx11=false` everything except the hotkey works: effects are still applied,
`enableOnLaunch` still decides the initial state, and toggling can be done by
editing the config and restarting the application.

## Making the loader find the layer

`-Dappend_libdir_vkbasalt=true` installs the library to
`$PREFIX/lib/vkbasalt/libvkbasalt.so` and writes an absolute `library_path` into
the manifest. Without it the manifest contains a bare `libvkbasalt.so`, which the
loader resolves relative to the executable, and that breaks under proot.

```sh
export VK_LAYER_PATH=$PREFIX/share/vulkan/implicit_layer.d
export ENABLE_VKBASALT=1
export VKBASALT_CONFIG_FILE=$PREFIX/etc/vkBasalt.conf
```

Notes:

- The manifest declares `enable_environment: ENABLE_VKBASALT=1`, so the layer
  stays inactive unless that variable is set.
- Recent Android loaders only honour `VK_LAYER_PATH` for debuggable processes.
  Inside a proot with a Termux/Mesa `libvulkan` this usually works; if not, use
  the system loader path or `VK_LOADER_LAYERS_ENABLE`.
- `VK_LOADER_DEBUG=layer` prints exactly which manifests were scanned and why a
  layer was rejected. Use it before changing anything else.

## Verifying

```sh
VK_LOADER_DEBUG=layer vkcube
```

Then set `upscaleFactor = 0.6667` and `effects = cas` in the config and run
again. The layer logs the reduced extent:

```
upscale: reporting render extent 1280x720 for surface 0x...
```

If the picture only fills the top left corner, the application ignored the
reported `currentExtent` and used the size it asked for. That is the case the
capabilities hook cannot fix; the application has to be driven by
`currentExtent` (most Vulkan titles and DXVK are).
