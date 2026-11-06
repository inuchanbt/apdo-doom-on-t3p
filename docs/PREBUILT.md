# ATORCH T3P DOOM V58.1

[日本語](PREBUILT.ja.md)

The kit contains the compiled DOOM app with Freedoom 0.13.0 E1M1 and a Python tool that assembles a DcTester update image offline. Supply your own exact ATORCH T3P 3.0.3 `.bin`. The supplied image contributes the bootloader, directory entries and resources. Keep the original file for recovery.

V58.1 uses the V58 play configuration and project-owned signed 64-bit division for the barometer. The V58.1 build and packing have been checked offline. Hardware boot, controls and recovery for this build are pending.

## Make the update image

Extract `t3p-doom-v58.1-kit.zip` and open PowerShell in the extracted directory. Obtain `T3P Firmware Version 3.0.3.bin` and DcTester from ATORCH. Substitute the path to the original T3P `.bin` below.

```powershell
py -3 .\scripts\package_prebuilt.py "C:\path\to\T3P Firmware Version 3.0.3.bin"
```

The script checks SHA-256 for the original, the DOOM app and the result, and refuses to overwrite files. The stock SHA-256 is `72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2`. `T3P_NATIVE_DOOM_E1M1_V58.1_3.0.3.bin` is the DcTester image. `DO-NOT-FLASH-t3p-doom-app.bin` is the packer's app input.

Leave both measurement connectors empty. Hold the left button while connecting the dedicated HID port to the PC, then select the generated image in DcTester. The DOOM app replaces the meter application. To return to the meter, use the same HID update screen with your original 3.0.3 image. Earlier hardware tests confirmed the stock HID restore path. Independent full-Flash recovery remains unverified. [Recovery record](RECOVERY_DECISION.ja.md)

## Rebuild the app from corresponding source

Extract `t3p-doom-v58.1-source.zip`. It includes the patched converter, its pinned doomgeneric inputs, the original Freedoom WAD archive with notices, and cached generated upstream tables. The editable Freedoom art and maps are available in the [Freedoom 0.13.0 source release](https://github.com/freedoom/freedoom/tree/v0.13.0). The pinned JieLi Windows compiler is a separate build tool. See `SOURCE_LOCK.json` for its origin and SHA-256.

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
py -3 scripts/prepare_lowres_game.py --converter-root ./converter
py -3 scripts/build_engine.py --toolchain $Pi32
py -3 scripts/build_native_board.py --toolchain $Pi32 --doom --revision 58 --release-app
```

Compare `artifacts/native-doom-v58-prebuilt/DO-NOT-FLASH-app-payload.bin` to the app digest in `release.json`. The release link uses project and vendored open-source objects. The LCD command table records the T3P panel register settings. See `COPYING`, `THIRD_PARTY_NOTICES.md` and the upstream licenses in the archive.
