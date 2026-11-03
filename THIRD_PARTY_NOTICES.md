# Source scope and third-party notices

The native DOOM integration and new native input/sensor code are
GPL-2.0-or-later. See `COPYING` and source headers. This is a research source
workspace, not a completed firmware or binary release.

The separate auxiliary-board diagnostic in `recovery/cm_input_check` is MIT
licensed, with its full license in that directory. It contains no DOOM engine,
game assets, or JieLi SDK code. Its local RP2350 build uses Raspberry Pi Pico
SDK 2.2.0 (BSD-3-Clause and individual source notices), TinyUSB (MIT), and the
Arm GNU toolchain runtime libraries under their individual licenses. Pinned
revisions and download hashes are in `SOURCE_LOCK.json`; retained SDK/TinyUSB
notices are in `notices/cm-input-check`. This is a local bench diagnostic,
not a public binary distribution or a T3P firmware.

The auxiliary programs in `recovery/cm_host_probe` and `recovery/cm_wave_scope`
are also MIT licensed under their retained `LICENSE` files. The host probe's
CDC descriptors are adapted from the MIT-licensed Pico-PIO-USB example and
retain its original copyright/license block. Its PIO USB dependency is pinned
in `SOURCE_LOCK.json`, retained under local research inputs, with its MIT
notice copied to `notices/cm-host-probe` and beside the generated local probe.
The scope program uses Pico SDK/TinyUSB without the PIO USB host library.

`vendor/dmc100-doom` is a subset of Artur Bieniek's DMC100-Doom, pinned in
`SOURCE_LOCK.json`, imported from the existing patched KM003C workspace.
Retain its `COPYING`, `LICENSES`, `CREDITS.md` and all individual copyright
notices, including id Software, Chocolate Doom, Raven, PrBoom-plus, doomgeneric,
Freedoom-derived generated material and the public-domain font. The retained
credits describe the full upstream project; not every file mentioned there is
present or compiled here. The existing local patch is already applied.
`native/doom_bridge.c` and `native/square_view.c` were adapted from the existing
KM003C project. The T3P change extends its input key map; hardware callbacks are
provided by the native board backend and simulated by the offline probe. No commercial DOOM IWAD is included.

The MIT-licensed jl-misctools subset retains `LICENSE` and `UPSTREAM.md` with
revision and local modifications. Analysis scripts derived from that tool
remain subject to its notice.

The tinf raw DEFLATE decoder is Copyright (c) 2003-2019 Joergen Ibsen, under
the zlib License. `vendor/tinf/LICENSE` and `UPSTREAM.md` retain the imported
source subset's notice, revision and scope. The surrounding page-pack reader
and cache are new GPL-2.0-or-later project code.

The local compressed game asset derives byte-for-byte from the existing
Freedoom 0.13.0 E1M1 conversion, pinned in `SOURCE_LOCK.json`. Its art remains
BSD-3-Clause, Copyright (c) 2001-2024 Contributors to the Freedoom project;
embedded engine strings and tables remain GPL-2.0-or-later. Original notices
are in `notices/freedoom-0.13.0` and copied beside generated game data. The
compression changes the storage format, not the game content or its licenses.

Manufacturer firmware, PC software, unpacked images, individual device data,
JieLi SDK sources, official toolchain binaries and extraction tools are local
research inputs under ignored `Atorch/` and `local/`. Their availability does
not grant this project a blanket redistribution license. SDK headers carrying
"not free / All right reserved" were consulted for structures and hardware
facts; SDK source was not copied into the project's new native source files.

The offline link probe uses the official toolchain's runtime archives as a
local experiment. Runtime redistribution terms and complete corresponding
source requirements must be resolved before any binary release. Its ELF is
not included in a public release and is not suitable for flashing.

`SOURCE_LOCK.json`, build manifests and the stock inventory record exact
inputs. A future release requires a separate explicit file inventory and
licensing review; ignored local research data is not a release asset.

The new MIT auxiliary `recovery/cm_rom_probe` uses the pinned Pico SDK,
TinyUSB and Pico-PIO-USB dependencies. Its native CDC descriptor source is
adapted from `recovery/cm_host_probe` and retains the upstream MIT notice.
SDK register definitions and ROM instructions were used as hardware research
evidence, without copying SDK implementation code into this probe.
