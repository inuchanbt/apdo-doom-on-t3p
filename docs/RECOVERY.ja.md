# バックアップ・復旧経路の調査

2026-10-31。実機の全Flashバックアップはまだ取得できていない。
配布更新ファイルは個体の設定・校正・鍵・ブート領域をすべて含むとは限らず、
その代用にはならない。以下はコードと資料の調査記録であり、実機検証済みの手順ではない。

## 現在の接続と別のブート経路

接続中のT3Pは`0483:5750`のベンダーHID、純正アプリ3.0.2。
JieLiのROM/UBOOTで使うUSBマスストレージ＋独自SCSI通信とは異なる。
通常アプリのHID更新コマンドを解明しただけで、ROMからの復旧が確保されたとは扱わない。
通常HIDでの設定・測定値の要求応答は[実機で確認できた](HID.ja.md)。
解析した通常の受信分岐では、全Flashを読み出す要求は見つかっていない。

## T3P自身の独自更新ブートローダー

全4版に共通する17,904バイトの`uboot.boot`を、SHA-256で固定して解析した。
ヘッダー16バイトの後に、RAMアドレス`0x102600`で動く17,888バイトのコードがある。
通常アプリの`55 05 09 02`は`ubootZst`タグを置く経路であり、
この独自HID更新処理とROMのマスストレージ型UBOOTを混同しない。

受信側はAAから始まる転送ヘッダーとCCから始まるデータを扱う。
有効なヘッダーを検査すると、`0x1046DA`で消去関数`0x102EA8`を呼び、
その後にBBの応答を組み立てる。消去関数はNORの0x20／0xD8などの命令を出す。
データ処理の`0x1047B0`はプログラム関数`0x1030C4`を呼ぶ。
したがって、更新ヘッダーを試しに送ってからバックアップを取る順序にはできない。
このブートローダーを実機で起動したり、転送ヘッダーを送ったりはしていない。

`scripts/uboot_evidence.py`が4版共通の指紋、11か所の命令と実バイト、
2個の相対呼出し先を照合し、`analysis/uboot-protocol.json`へ記録する。

逆アセンブルの再生成もローカルファイルだけで行う。

```powershell
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\stock_elf.py 'local\unpacked\T3P Firmware Version 3.0.3.bin\top\uboot.boot' local\uboot-analysis.elf --base 0x102600 --offset 0x10
& '.\local\tools\jieli\C$\JL\pi32\bin\llvm-objdump.exe' -disassemble -print-imm-hex local\uboot-analysis.elf | Out-File -Encoding utf8 analysis\uboot.disasm.txt
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\uboot_evidence.py
```

同系統BR35バッジの研究では、JieLi用更新ドングルでROMモードへ入り、
RAMへFlash読取りローダーを置く経路がある。
T3Pの基板、鍵、Flash設定にそのまま流用できることは未確認。
別機種のファーム、個体鍵、ローダー引数をT3Pへコピーしない。

## 調べたツールの挙動

[HeikkiRadu/jl-uboot-tool](https://github.com/HeikkiRadu/jl-uboot-tool)
の`main`をソースとして調査し、実行はしていない。
調査時の先頭コミットは`ef77f9c36abf577c568684661089aadbdd4b6f59`。
BR35のRAMローダーは`0x102600`に配置する設定だが、同じ配置先というだけで
T3Pへの適合性を確定しない。

- `docs/how-to-enter-uboot.md`は更新ドングルによるUSB_KEY信号を説明している。
  これは通常のHIDレポート送信ではない。
- `jltech/uboot.py`の汎用デバイスopenは、標準のデバイス情報読取り以外にも
  `online_device()`を自動送信する場合がある。無条件の自動探索には使わない。
- `jluboottool.py`は`UBOOT1.00`を検出すると、コマンドシェルへ入る前に
  `mem_write()`と`mem_jump()`でRAMローダーを転送・実行する。
  したがって、シェルで`read`だけ指定しても、接続からずっと受信だけの動作にはならない。
- Flash読取りは`flash_read()`。同じツールには書込み・消去も含まれる。
  現段階で汎用CLIをT3Pに接続することはしていない。

## 実機へ進むために残る確認

ユーザーの所有品はRP2350B-Plus-W、RP2350-USB-CM、M5Stack CoreS3。
[CM版の準備](CM_BOARD.ja.md)と[DIY接続の調査](DIY_RECOVERY.ja.md)を進めている。
[CM経由の通常HID通信](CM_USB_BRIDGE.ja.md)は実機で成功。
現在は[スコープ用の候補波形](CM_WAVE_SCOPE.ja.md)を準備中で、T3PのROM接続は未達。

1. T3PでROMモードへ入る再現可能な方法。専用ドングルの有無も確認する。
2. 実際のUSB識別と、対象をその本体だけに限定する方法。
3. BR35用ローダーの内容・ハッシュ・RAM配置・SPI NOR設定の照合。
4. Flash IDとサイズ、消去・書込みを含まない読取り経路の確認。
5. 個体全体を二度取得して一致を確認し、元ファイルとハッシュを保存。
6. 純正に戻せる構成を確かめてから、最小表示の試験用イメージを作る。

資料:

- [同系統バッジでのバックアップ研究](https://github.com/ElectronicCats/jieli-ble-badge-research/blob/main/docs/dump-firmware.md)
- [UBOOT進入方法の説明](https://github.com/HeikkiRadu/jl-uboot-tool/blob/main/docs/how-to-enter-uboot.md)
- [ツールの接続・RAMローダー処理](https://github.com/HeikkiRadu/jl-uboot-tool/blob/main/jluboottool.py)
- [通信の実装](https://github.com/HeikkiRadu/jl-uboot-tool/blob/main/jltech/uboot.py)
