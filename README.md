# DOOM on ATORCH T3P

[日本語](README.ja.md)

This experimental native port runs Freedoom E1M1 on the ATORCH T3P's own 240×240 LCD. The left physical button fired the weapon in a hardware test. The current milestone is **V26**, tested on the device.

## Bring-up and performance

- Stock USB HID reads worked first. Holding the left button while applying power opens the stock HID update screen. Reflashing official 3.0.3 and returning to normal USB operation were verified. A full T3P Flash backup and independent ROM recovery are still unverified.
- LCD tests progressed from color bars to RGB565 geometry checks and standalone board initialization. Native DOOM then rendered the E1M1 start room and HUD.
- Early profiled builds took about 7.8–7.9 seconds per frame. V12 improved DEFLATE decoding, and V13 isolated repeated decompression as the largest measured cost. V14 added a cache for flats and drawable columns. V16 and V20 stored selected frequently decoded pages uncompressed while retaining byte-identical game data.
- V17 showed noise during the first DBI DMA trial. V18 restored the game image with the corrected active-line setup and measured 134–138 ms of LCD time versus roughly 310–320 ms on V16. V19 overlapped DBI DMA with rendering using two four-column buffers.
- Two V20 samples took about **0.87–0.93 s per frame**, including about **0.59–0.63 s of DEFLATE decoding**. These are diagnostic snapshots from the start area. The left-to-right redraw is still visible. Touch, motion sensors, audio and full game controls remain unfinished.

- V21 buffers a complete 120×120 indexed frame and upscales it to 240×240 at presentation. The left-to-right redraw became less noticeable, though it is still visible during firing. V22 and V23 bypass DEFLATE for five more hot pages. V23 measured **1,217 ms initially, 966 ms after settling, and 1,031 ms around firing**. The interval from first band submission through final DMA completion was **162–163 ms**.

- V24 changed the DBI divider selector from 3 to 2. Measured presentation dropped to **83–84 ms**, and firing redraw became less noticeable. The earlier nominal 8 MHz estimate is unverified because the observed timing did not match that estimate.
- V25 stored seven additional pages uncompressed. Its initial, settled and firing snapshots measured **1,040 / 704 / 900 ms per frame**, with **642 / 436 / 560 ms** of decoding. V26's four-byte match-copy experiment showed no clear improvement in the three photo samples.
- **V27 is prepared and awaiting a device result.** It uses a 7-bit Huffman prefix table, with the V26 match-copy option disabled. Host checks covered 1,369 pages, 5,000 random reads and 420 streams including stored-block transitions.

## Repository layout

| Directory | Contents |
|---|---|
| native | Board startup, LCD/DBI, asset reader and engine bridge |
| scripts, tests | Build, offline checks and research tooling |
| recovery | RP2350 helper firmware source |
| docs, analysis | Research notes and selected build/hardware evidence |
| vendor, patches, notices | Imported source, modifications and license notices |

Manufacturer downloads, proprietary SDK/toolchain files, game packs, firmware images and raw bench captures are local inputs or outputs. Historical Japanese research notes can refer to those local files.

## Build locally

Use Python 3, the pinned JieLi Windows pi32 toolchain, your own official T3P firmware 3.0.3, and the converted Freedoom 0.13.0 E1M1 DOOM.DAT identified in [SOURCE_LOCK.json](SOURCE_LOCK.json). The raw pack is 2,802,000 bytes. The converter and data preparation belong to the upstream/KM003C workflow. Put the stock image at `Atorch/T3P Firmware Version 3.0.3.bin`.

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
$GameData = 'C:/path/to/E1M1/DOOM.DAT'
python vendor/jl-misctools/firmware/fwunpack_newfw.py --dirname "local/unpacked/T3P Firmware Version 3.0.3.bin" "Atorch/T3P Firmware Version 3.0.3.bin"
python scripts/pack_game.py --input $GameData
python scripts/build_engine.py --toolchain $Pi32
python scripts/build_native_board.py --toolchain $Pi32 --game-data $GameData --doom --package-doom --revision 23
```

The last command generates an experimental update image under `artifacts/native-doom-v23/`. Files marked `DO-NOT-FLASH` are build intermediates. See [recovery notes](docs/RECOVERY.ja.md) before testing on hardware, [V23 build evidence](analysis/native-doom-v23-build-reference.json) for the reference build, and [third-party notices](THIRD_PARTY_NOTICES.md) for license scope.
