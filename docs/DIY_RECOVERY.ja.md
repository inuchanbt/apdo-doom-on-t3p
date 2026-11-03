# 開発ボードを使うROM復旧接続の準備

2026-10-31。所有品はRP2350B-Plus-W、RP2350-USB-CM、M5Stack CoreS3と判明。
[RP2350-USB-CMの入力検査](CM_BOARD.ja.md)と、
[通常HID通信の中継](CM_USB_BRIDGE.ja.md)に成功した。
T3PのROMモードへの接続・全Flash読戻しは未実施。

## 公式ROMから増えた根拠

固定済み公式SDKの`SDK/cpu/br35/tools/rom.image`を追加取得した。
32,768バイト。SHA-256は
`12341f7db1d764b865466c8d54447b53d8faabe9867ec728752d44085dd0c8f7`。
これはSDKに同梱されたBR35の参照ROMで、接続中のT3Pから取得したものではない。

`0xFA11E`の受信処理はUSBポートの入力ビットを16bitのシフトレジスターへ取り込み、
`0xFA148`で`0x16EF`と比較する。シフトレジスターは`0x10130C`。
キー一致後にはUSB端子の出力・方向を設定する処理がある。
受信シフトの解釈はMSBから`0001011011101111`。
外部のタイミング、応答の長さ、基板配線はこの即値だけでは確定しない。

同ROMにはUSB識別`4C4A:2942`と、SCSI問い合わせ文字列
`BR35    UBOOT1.00       1.00`もある。
通常アプリ`0483:5750`とは別の機器として現れることを確認する必要がある。

公式SDKの`br35loader.bin`は27,328バイト、SHA-256は
`1295a01d5a89ad42e9eda6b87acfe7d543ab68293d4087c3dda9bf9ab472dae1`。
取得・指紋記録だけを行い、実機へ転送・実行していない。

再現:

```powershell
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\stock_elf.py local\jieli-sdk\SDK\cpu\br35\tools\rom.image local\sdk-rom-analysis.elf --base 0xf8000
& '.\local\tools\jieli\C$\JL\pi32\bin\llvm-objdump.exe' -disassemble -print-imm-hex local\sdk-rom-analysis.elf | Out-File -Encoding utf8 analysis\sdk-rom.disasm.txt
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\rom_usb_evidence.py
```

24か所の命令と実バイト、3相対呼出し先、SDKのUSB入力ビット定義、
参照ROM・ローダーのハッシュ、USB識別を照合し、
`analysis/rom-usb-evidence.json`へ記録する。

## 外部の試作との比較

[ElectronicCatsのPico試作](https://github.com/ElectronicCats/jieli-ble-badge-research/tree/main/tools/pi-pico-jl-dongle)
はUSB_KEY送信・応答検出・USB引渡しを段階的に検証する計画で、
READMEでも最終の引渡し方法が未確定とされている。
公開されていることを、T3Pで使用できる完成済みドングルの証明にはしない。

そのCコードはGPIOをpush-pullで駆動した後に入力へ切り替える。
応答検出は実時間の測定ではなく1µs待機ループの回数を数えている。
125MHzを前提とした待機時間の組立てもあるため、別ボードへそのまま移植しない。
内蔵プルアップだけの判定や、相手機器との同時駆動は検証が必要。

先行研究の説明では、キー送信の後はPCからUSB信号を供給する必要がある。
想定する構成は次の順序。

1. PCのD+/D-からT3Pを切り離し、開発ボード側へ接続する。
2. T3Pの起動時にUSB_KEYを送り、応答を確認する。
3. 開発ボードの出力を解放し、データ線をPCへ切り替える。
4. PCがROM機器を認識したら対象を識別し、バックアップ用の処理へ進む。

T3P・PC・GPIOのデータ線を常時並列接続する構成にはしない。
切替方法の候補には、USBデータ用スイッチを使う方法がある。
例として[TI TS3USB221Eの資料](https://www.ti.com/lit/ds/symlink/ts3usb221e.pdf)は
双方向のUSB 1:2切替と出力無効化を規定しているが、部品・回路の採用はまだ決めていない。
電源経路もデータ線の切替とは分けて確認する。

CM版の2ポートを使うソフトウェア中継も候補に加え、GPIO電圧、波形の作り方、
接続切替、手元の測定器に合わせた検証方法を決める。
通常HIDの固定読取りはCM経由でも確認済み。T3PへUSB_KEY信号はまだ送っていない。
[スコープ単独確認](CM_WAVE_SCOPE.ja.md)を準備している。

2026-11-01追記: SDK入力定義との照合で、キー候補をD+クロック／D−データへ修正。
修正版の実測と、PIO/DMAを解放してUSBホストを開始するベンチ試験に成功した。
[一回接続のROM識別試験](CM_ROM_PROBE.ja.md)を実装したが、初回待機は未発火で期限切れ。
T3Pへのキー送信は0回で、ROM接続・全Flash読戻しは未達のまま。
