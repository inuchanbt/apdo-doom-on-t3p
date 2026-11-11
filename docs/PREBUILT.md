# ATORCH T3P DOOM V66

[日本語](PREBUILT.ja.md)

The kit contains the compiled DOOM app with Freedoom 0.13.0 E1M1, an offline Python packer and the PC keyboard/live-capture controller. Supply your own exact ATORCH T3P 3.0.3 image. The packer preserves its bootloader and resources. Keep that original for recovery.

The owner played from the PC preview using the private-ID V66 build. The public build uses shared HID 16c0:05df with manufacturer usb.pd.epr.240w@gmail.com and product T3P DOOM V66. Its descriptor and host matching have offline tests. Hardware enumeration of this public identity remains pending. [Hardware record](../analysis/native-doom-v66-physical-result.json) · [Shared-ID conditions](USB_ID.md).

## Generate and install the update

1. Obtain the original T3P 3.0.3 image and DcTester from ATORCH.
2. Extract t3p-doom-v66-kit.zip and open PowerShell in that folder.
3. Substitute your original firmware path in this command.

```powershell
py -3 .\scripts\package_prebuilt.py "C:\path\to\T3P Firmware Version 3.0.3.bin"
```

The result is T3P_NATIVE_DOOM_E1M1_V66_3.0.3.bin. The DO-NOT-FLASH-t3p-doom-app.bin in the kit is input for the packer. The packer checks the stock, app and final image against SHA-256 and refuses to overwrite an existing file. Stock 3.0.3 SHA-256 is 72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2.

Leave the measurement connectors empty. Hold the left button while connecting the dedicated side HID port to open the stock update screen. Use DcTester to write the generated image. To return to the meter, enter the same update screen and write the original 3.0.3. Earlier stock HID recovery was verified. The V66-to-stock round trip is pending. [Recovery record](RECOVERY_DECISION.ja.md).

## Play from the PC

With DOOM running, connect the side HID port to Windows and close other T3P controllers.

```powershell
py -3 -m pip install hidapi
py -3 .\scripts\t3p_pc_controller.py
```

Open [the local controller](http://127.0.0.1:50595/) and enable the keyboard. WASD/arrows move and turn, Space fires, E uses doors and switches, and Esc/Enter operate the menu. Fast 60×60 is the default, with Detail 120×120 selectable. Pause the preview or save a BMP from the toolbar. Leaving the tab releases input. The screen frame also works from the kit alone. Use the optional --shell path/to/t3p.png for your own compatible enclosure photo.

A zero-input enumeration and echo check is available with py -3 .\scripts\t3p_pc_keyboard.py --probe. The host supports both the public shared-ID build and earlier private test builds. For a fork with another manufacturer string, pass --manufacturer with its complete value.

## Rebuild from corresponding source

Extract t3p-doom-v66-source.zip. It contains the modified converter, its pinned doomgeneric sources, Freedoom input ZIP and generated upstream tables. Obtain the separately distributed JieLi Windows pi32 toolchain listed in SOURCE_LOCK.json.

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
py -3 scripts/prepare_lowres_game.py --converter-root ./converter
py -3 scripts/build_engine.py --toolchain $Pi32
py -3 scripts/build_native_board.py --toolchain $Pi32 --doom --revision 66 --release-app --usb-id 16c0:05df --usb-manufacturer usb.pd.epr.240w@gmail.com
```

The app is artifacts/native-doom-v66-prebuilt-public/DO-NOT-FLASH-app-payload.bin. Compare its SHA-256 to payload_sha256 in release.json. The release uses project and vendored open-source objects and omits all JieLi runtime archives. The original Freedoom art and maps are available in the [0.13.0 source release](https://github.com/freedoom/freedoom/tree/v0.13.0). Retain COPYING, THIRD_PARTY_NOTICES.md and all upstream notices.
