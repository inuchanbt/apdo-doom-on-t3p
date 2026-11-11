# DOOM on ATORCH T3P

[日本語](README.ja.md)

[Prebuilt app and offline packaging](docs/PREBUILT.md)

This experimental native port runs Freedoom E1M1 on the ATORCH T3P's 240×240 LCD. The current build boots from a centered Freedoom banner straight into E1M1. The game runs on the T3P with its built-in buttons, touchscreen and sensors.

## Play on the T3P

Hold the T3P with its USB-C plug pointing up. After the game appears, hold it still for about a second to set the neutral tilt position.

| Input | Action |
|---|---|
| Left button | Tap to fire once or hold for repeated fire. In the menu it confirms. |
| Right button | Use doors and switches. In the menu it goes back. |
| Both buttons for 700 ms | Open or close the menu. |
| Touch and drag | Move and turn. Drag vertically to navigate the menu. |
| Tilt | Lean left or right to turn, and forward or backward to move. Touch takes priority. |
| Clap | Fire one shot after the microphone adapts to ambient sound. |

Firing a weapon and taking damage use distinct vibration pulses. On hardware, a clap fired once and the motor did not trigger another shot. The menu's **UAC MONITOR** shows pressure in hPa and altitude relative to startup. A sustained rise of at least 3 m triggers `UPPER DECK FOUND`. The pressure and altitude behavior was checked with simulated readings. [Controls and diagnostics](docs/CONTROLS.ja.md).

## V66 PC controls and live capture

The owner played on a real T3P using the browser preview. V65 already streamed video while accepting input, but was too slow to play by watching the PC. V66 defaults to **Fast 60×60** and also offers **Detail 120×120**. The browser reports the rate of received frames and the time to acquire the last frame. The hardware result was qualitative.

Install Python and the HIDAPI binding on Windows. With the V65 or V66 test firmware running, connect the dedicated side HID port and close any other T3P controller.

```powershell
py -3 -m pip install hidapi
py -3 scripts/t3p_pc_controller.py
```

Open [the local controller](http://127.0.0.1:50595/) and enable keyboard input. Use WASD or the arrows to move and turn, Space to fire, E to use a door or switch, and Esc/Enter for the menu. You can pause the preview or save the latest frame as a BMP.

Download the **V66 prebuilt app and offline packer kit** from [Releases](https://github.com/inuchanbt/apdo-doom-on-t3p/releases/tag/v66). Supply your own exact stock T3P 3.0.3 file to generate the update image locally. The release also includes the corresponding source and PC controller. [Install and rebuild](docs/PREBUILT.md).

The public build uses shared HID 16c0:05df and matches the complete manufacturer and product strings. Its manufacturer is usb.pd.epr.240w@gmail.com, confirmed by the publisher. The shared-ID descriptors have offline checks. Hardware gameplay and playable capture were confirmed with the earlier private-ID V66 build. The public USB identity still needs hardware verification. [Identity conditions](docs/USB_ID.md).

## Bring-up and performance

- Stock USB HID reads worked first. Holding the left button while applying power opens the stock HID update screen. Reflashing official 3.0.3 and returning to normal USB operation were verified. A full T3P Flash backup and independent ROM recovery are still unverified.
- LCD tests progressed from color bars to RGB565 geometry checks and standalone board initialization. Native DOOM then rendered the E1M1 start room and HUD.
- Early profiled builds took about 7.8–7.9 seconds per frame. V12 improved DEFLATE decoding, and V13 isolated repeated decompression as the largest measured cost. V14 added a cache for flats and drawable columns. V16 and V20 stored selected frequently decoded pages uncompressed while retaining byte-identical game data.
- V17 showed noise during the first DBI DMA trial. V18 restored the game image with the corrected active-line setup and measured 134–138 ms of LCD time versus roughly 310–320 ms on V16. V19 overlapped DBI DMA with rendering using two four-column buffers.
- Two V20 samples took about **0.87–0.93 s per frame**, including about **0.59–0.63 s of DEFLATE decoding**. These are diagnostic snapshots from the start area. The left-to-right redraw was still visible at that stage.

- V21 buffers a complete 120×120 indexed frame and upscales it to 240×240 at presentation. The left-to-right redraw became less noticeable, though it is still visible during firing. V22 and V23 bypass DEFLATE for five more hot pages. V23 measured **1,217 ms initially, 966 ms after settling, and 1,031 ms around firing**. The interval from first band submission through final DMA completion was **162–163 ms**.

- V24 changed the DBI divider selector from 3 to 2. Measured presentation dropped to **83–84 ms**, and firing redraw became less noticeable. The earlier nominal 8 MHz estimate is unverified because the observed timing did not match that estimate.
- V25 stored seven additional pages uncompressed. Its initial, settled and firing snapshots measured **1,040 / 704 / 900 ms per frame**, with **642 / 436 / 560 ms** of decoding. V26's four-byte match-copy experiment showed no clear improvement in the three photo samples.
- V27 added a 7-bit Huffman prefix table, with the V26 match-copy option disabled. Host checks covered 1,369 pages, 5,000 random reads and 420 streams including stored-block transitions.
- V28 and V29 compare the original and prefix decoders after 10 warmup frames over 30 measured frames. Mean frame time was **753 → 698 ms** and mean decoding **496 → 452 ms**, with presentation around **83 ms**. These are one run per variant, without a fixed simulation replay. Buttons are disabled and the final screen freezes intentionally in these benchmark builds. [Results](analysis/native-doom-v28-v29-benchmark.json).
- **V30** restores regular left-button fire and continuous play using the V29 decoder. Its firmware is byte-identical to V27: it adds no further optimization beyond the prefix decoder now supported by benchmark evidence. The user confirmed firing and continued gameplay on V30; elapsed test duration was not measured. [Hardware result](analysis/native-doom-v30-physical-success.json).

- **V31 rendered gameplay and firing on the device, with no clear perceived speedup over V30.** It widens the Huffman lookup from 7 to 8 bits, adding 512 bytes of decoder stack use while retaining the V30 game pack, caches and regular controls. More symbols can use one lookup, but table setup costs also grow; the three device snapshots do not establish an averaged gain. V30 served as the measured baseline. [Device result](analysis/native-doom-v31-physical-result.json). Host checks passed all 1,369 pages, 5,000 random reads and 864 streams for each lookup width. Run `python scripts/check_tinf_prefix.py --compiler <host-clang>` for the decoder checks.

- **V32 rendered gameplay and firing on the device; speedup remains unconfirmed.** A hybrid pack converts three raw donor pages and four frequently decoded DEFLATE pages to LZ4. The asset pack shrinks by 1,828 bytes; the native build retains 2,052 bytes of Flash headroom and the same static RAM use. Other page payloads and gameplay controls remain unchanged. The user tentatively felt an improvement, but the initial/settled/firing frame snapshots were 985/756/880 ms versus V31's 977/738/761 ms. These snapshots capture unmatched single frames. V30 served as the measured baseline. [Device result](analysis/native-doom-v32-physical-result.json). [Format and selection](docs/GAME_PACK_FORMAT.md), [build reference](analysis/native-doom-v32-build-reference.json).

- **V33/V34 completed the paired device benchmark.** Both measured 30 frames after 10 warmup frames, ending at game tic 40 with the same indexed-image sequence fingerprint (`C269CBCE`). The hybrid pack reduced mean frame time **668 → 640 ms (4.2%)** in this controlled workload. Combined decoding increased **427 → 443 ms**, while presentation stayed **83 ms**; the total improvement cannot be attributed to faster decoding. These are one run per variant, with input disabled and one engine tic per frame, rather than normal-play FPS. V30 served as the gameplay baseline. [Results](analysis/native-doom-v33-v34-benchmark.json), [procedure and display rows](docs/BENCHMARK_V33_V34.ja.md).

- **V35/V36 completed the breakdown benchmark.** Both completed 30 frames at game tic 40 with fingerprint `C269CBCE`. The instrumented hybrid run was slightly slower: **676 → 693 ms/frame**, with **428 → 444 ms decoding**, **17 → 20 ms compressed fetch**, **2 ms raw Flash copies**, **6 ms cached-page copies**, **0 ms first-use CRC**, and **83 ms presentation**. Inclusive rendering was 570/587 ms and includes asset reads. This does not reproduce the earlier modest hybrid advantage; one run and extra timers/code-placement effects do not establish a general codec win. The following trials used V30 as the gameplay baseline and measured decode totals by page. Lower byte/call rows were not transcribed from these photos. [Results](analysis/native-doom-v35-v36-benchmark.json), [procedure and rows](docs/BENCHMARK_V35_V36.ja.md).

- **V37/V38 cumulative hot-page probes completed on hardware.** At the same 30 measured frames, conventional V37 averaged 668 ms/frame (428 ms decode); hybrid V38 averaged 686 ms/frame (444 ms decode). Both finished at tic 40 with fingerprint `C269CBCE`; presentation stayed 83 ms. The top eight compressed pages account for only about 84/91 ms per frame, so decode load is spread across many pages. This pair does not demonstrate a hybrid speed advantage. One run per variant, with profiling and changed code/RAM layout. [Results and page rankings](docs/BENCHMARK_V37_V38.ja.md).

- **V39/V40 hardware comparison completed; no demonstrated speedup from the SRAM hot loops.** Both completed the same 30-frame sequence at tic 40 with fingerprint `C269CBCE`. Flash V39 averaged 666 ms/frame (425 ms decode); SRAM V40 averaged 668 ms/frame (430 ms decode); presentation stayed 83 ms. Decoder objects are identical, but moving these 446 code bytes did not yield a speed advantage. One run each and a small difference do not establish a regression. V30 served as the normal-play baseline. [Results and page rankings](docs/BENCHMARK_V39_V40.ja.md).

- **V41/V42 completed on hardware: compressed payload dominates.** Both matched tic 40 and fingerprint `C269CBCE`. Coarse V41 measured about 95 ms/frame preparing trees versus 382 ms decoding payload. V42 helper sampling added substantial overhead; naive symbol-time extrapolation exceeds the full decode interval and cannot establish helper shares. Next controlled comparison removes these probes and batches short literals. [Measurements and limitations](docs/BENCHMARK_V41_V42.ja.md).

- **V43/V44 completed the controlled hardware comparison: a measured improvement from literal runs.** Both completed 30 measured frames at tic 40 with fingerprint `C269CBCE`. V44 reduced mean frame time **666 → 626 ms (6.01%)** and decoding **430 → 387 ms (10.0%)**; presentation stayed **83 ms**. V43 uses the byte-identical V39 decoder; V44 adds 96 Flash bytes and no static RAM. The pack, caches, clocks and display path match. Each variant was run once; binary placement may contribute, and these fixed-tic start-room results are not general normal-play FPS. The reciprocal mean rate is only about 1.50 → 1.60 frames/s, so further improvement is needed. The next build used V44's literal runs, with V30 as the verified continuous-play baseline. [Results](analysis/native-doom-v43-v44-benchmark.json), [procedure and validation](docs/BENCHMARK_V43_V44.ja.md).

- **V45/V46 completed the controlled hardware comparison: fully uncompressed assets delivered a large improvement.** Both completed 30 measured frames at tic 40 with fingerprint `EBC8D404`. V46 reduced mean frame time **1,147 → 205 ms (82.13% shorter; about 5.60 times the reciprocal rate)**, removing **864 ms** of mean DEFLATE time; presentation stayed **99 ms**. Against the earlier V44 result (626 ms), V46 is about **3.05 times** the reciprocal rate, with reduced texture/sprite detail as well as storage changes. Its reserved-budget RAM headroom is **81,152 bytes**. Each variant was run once; about **4.88 frames/s** is the reciprocal mean for this fixed-tic start-room benchmark, not general normal-play FPS. V30 served as the gameplay baseline during this comparison. [Hardware results](analysis/native-doom-v45-v46-benchmark.json), [changes, procedure and validation](docs/BENCHMARK_V45_V46.ja.md).

- **V47/V48 completed on hardware: overlapped rendering and DMA reduced mean frame time 215 → 145 ms (32.56% shorter; about 1.48 times the reciprocal mean rate).** Both completed 30 measured frames at tic 40 with fingerprint `EBC8D404`, using the same raw assets, two indexed frames, palette snapshots, 16-column transfer and clock settings. The checksum and final DMA drain are timed; intermediate diagnostics are removed. The reciprocal rates of about 4.65 → 6.90 frames/s describe this fixed-tic start-room test, not general normal-play FPS. Each variant was run once in the start room. V48 became the presentation baseline for V49. [Hardware results](analysis/native-doom-v47-v48-benchmark.json), [implementation and validation](docs/BENCHMARK_V47_V48.ja.md).

- **V49 passed the device gameplay trial with V48's overlapped presentation.** The left button fired one shot on a tap and fired repeatedly when held. The player punched after running out of ammunition. The user reported responsive controls and over two minutes of continuous operation. Normal-play FPS is unmeasured. The serial and overlap host runs matched through 668 steps and 2,746 tics. V49 has 54,832 bytes of reserved-budget RAM headroom. [Device result](analysis/native-doom-v49-physical-success.json), [procedure and validation](docs/PLAY_V49.ja.md).

- **V50–V55 made E1M1 playable with the built-in controls.** V50 corrected the first-person weapon placement. V51 added touch drag, and V52 added door use and the two-button menu. V53–V55 brought up tilt and clap input and added distinct vibration for firing and damage.
- **V56–V58 finished the current presentation.** V56 added UAC MONITOR, corrected the roll direction and reduced tilt sensitivity. V57 scales the view from 200 to 208 LCD rows to meet the 32-row HUD. V58 reuses the KM003C port's Freedoom logo and presents it at 216×50 on black during map initialization. The banner stays visible for at least three seconds by design. The user confirmed the V58 startup on the device. [Build and hardware record](analysis/native-doom-v58-build-reference.json).

## Repository layout

| Directory | Contents |
|---|---|
| native | Board startup, LCD/DBI, controls, asset reader and engine bridge |
| assets | Freedoom startup logo and its provenance |
| scripts, tests | Build, offline checks and research tooling |
| recovery | RP2350 helper firmware source |
| docs, analysis | Research notes and selected build/hardware evidence |
| vendor, patches, notices | Imported source, modifications and license notices |

Manufacturer downloads, proprietary SDK/toolchain files, game packs, firmware images and raw bench captures are local inputs or outputs. Historical Japanese research notes can refer to those local files.

## Build V58 locally

Use Python 3, the pinned JieLi Windows pi32 toolchain and the official T3P 3.0.3 image. See [SOURCE_LOCK.json](SOURCE_LOCK.json) for the pinned versions and hashes. Keep the exact source line endings with `git clone -c core.autocrlf=false https://github.com/inuchanbt/apdo-doom-on-t3p.git`. The packaging checks use byte hashes of the tested board sources.

Prepare a full DMC100-Doom checkout at the pinned revision and run its `make gen` with the upstream host build tools, for example under Linux or WSL. The T3P asset builder reads `build/conv/upstream.json` and `dl/freedoom-0.13.0.zip` from that checkout. Place the stock image at `Atorch/T3P Firmware Version 3.0.3.bin` and run the following in the T3P checkout.

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
$Converter = 'C:/path/to/DMC100-Doom'
python scripts/prepare_lowres_game.py --converter-root $Converter
python vendor/jl-misctools/firmware/fwunpack_newfw.py --dirname "local/unpacked/T3P Firmware Version 3.0.3.bin" "Atorch/T3P Firmware Version 3.0.3.bin"
python scripts/build_engine.py --toolchain $Pi32
python scripts/build_native_board.py --toolchain $Pi32 --doom --package-doom --revision 58
```

The builder generates `artifacts/native-doom-v58/T3P_NATIVE_DOOM_E1M1_V58_3.0.3.bin`. Verify its SHA-256 against the [V58 build record](analysis/native-doom-v58-build-reference.json). Files marked `DO-NOT-FLASH` are build intermediates. For hardware tests, use the dedicated HID port with the measurement ports disconnected. Holding the left button at power-up opens the stock HID updater. To return to meter use, restore official 3.0.3 with DcTester. [Verified stock recovery](docs/RECOVERY_DECISION.ja.md), [third-party notices](THIRD_PARTY_NOTICES.md).
