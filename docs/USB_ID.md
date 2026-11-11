# Shared HID identity for the V66 kit

V66 uses Objective Development's shared generic-HID ID, 16c0:05df. Its published conditions allow matching by the complete manufacturer and product strings and use with other USB implementations. The firmware supplies both strings in English/US. The controller checks both strings in full, the VID/PID and its HID usage before opening a device. It uses the operating system's HID class driver. This interface exchanges application reports for controls and images.

The release manufacturer string identifies an email address controlled by the publisher. That address must remain under their control while these builds are in use. The product string is T3P DOOM V66. If you redistribute a modified build with a different manufacturer or product, update the firmware and host matching together and satisfy the shared-ID conditions.

[Shared-ID conditions](https://raw.githubusercontent.com/obdev/v-usb/master/usbdrv/USB-IDs-for-free.txt) · [Other USB implementations](https://raw.githubusercontent.com/obdev/v-usb/master/usbdrv/USB-ID-FAQ.txt)

## 日本語

V66配布版は共用の汎用HID ID 16c0:05dfを使う。上記の利用条件に合わせ、製造者と製品名を英語の文字列で返し、PC側で両方を完全一致照合する。OS標準のHIDドライバーで通信する。

製造者文字列のメールアドレスは、このビルドが使われている間、公開者が継続して管理する。製品名はT3P DOOM V66。変更版を配布する場合も、ファームとPC側の照合を合わせて共用IDの条件を満たす必要がある。

配布版の共用IDによる動作も、T3Pへ書き込んで実機確認した。従来の非公開テスト版1209:0001とはUSB識別情報とハッシュが異なる。
