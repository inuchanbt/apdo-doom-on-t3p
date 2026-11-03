# CMを経由したT3PのUSB通信

2026-11-01。通常のUSB認識と、固定のHID読取り要求の往復を実機で確認した。
T3PのROM復旧接続・全Flashバックアップ・DOOMの本体実行はまだ未達。

## 実機の構成

ユーザーによる確認事項:

- T3PのPC接続専用端子はUSB-Cメス。従来のPCケーブルはUSB-AからUSB-C。
- T3Pの測定用端子には充電器・負荷を接続しておらず、給電元はPCだけ。
- 従来のT3P用ケーブルを外し、CMのオスをT3PのPC接続専用端子へ接続。
  CMのメスをPCへ接続したままで、T3Pの画面が点灯した。

```text
PC ── CMのメス（通常USB CDC）
      CMのオス（GPIO13=D+ / GPIO12=D−、PIO USBホスト）── T3PのPC接続専用USB-C
```

CMのA（R10と判断した1.5kΩプルアップ）は取り外し済み。
CC抵抗は変更していない。回路図上は両VBUSがVSYSで共通であり、
今回はその固定給電の実験構成で起動・通信が成立したという結果。
USB-CのSource広告や個別電源スイッチを実装した結果ではなく、一般のUSB-C機器や
別電源を併用する接続の適合性をこの試験から保証しない。

最初に入力検査v2で、接続後のD+=1/D−=0を4件受信。プル設定は無効。
その後、別のプログラムで通常のUSB認識へ進んだ。

## 識別専用版（v1）

`recovery/cm_host_probe`の最初の版をCMへ書込み照合し、PC側はCOM12、
VID/PID=`CAFE:4001`のCDCとして動作した。v1ソースは
`artifacts/cm-host-probe/source`へ保存してから、後述のv2へ拡張した。

起動時はGPIO12/13を入力・内蔵プル無効とし、`HOST_START`を受けた時だけ
core1でPIO USBホストを起動。core0は通常USB CDCを処理する。
CMのシステムクロックは120MHz。これはT3PのCPU周波数ではない。

実機のVID/PIDは`0483:5750`。HIDレポートディスクリプター34バイトは、
配布ファーム4版から照合済みの次の値と完全一致した。

```text
0602ff0902a1010903150026ff007508954081000904150026ff00750895409100c0
```

この段階はバスリセット、通常のUSB列挙、HIDのSET_IDLEと記述情報の読取りだけ。
HIDアプリ要求、更新要求、USB_KEY、SCSIやRAMローダーは含まなかった。
生の受信は`local/cm-t3p-descriptor-probe.json`。

## 固定読取り版（v2）

v2は同じHIDデータ・VID/PIDの一致をcore1側で確認してから、
PCの`Q_SETTINGS`/`Q_MEASUREMENT`指示に対応する固定パケットだけを送る。
任意バイト送信の入口はない。

| 要求 | 64バイト内の先頭9バイト（残りは0） |
| --- | --- |
| 設定読取り | `55 05 09 03 00 00 00 EE FF` |
| 測定値読取り | `55 05 22 05 0B 00 8C EE FF` |

TinyUSBのInterrupt OUTをReport ID=0で使用する。
実際に送るのは64バイトで、PCのHIDAPI用の先頭ダミー0を付け足さない。
両パケットは既に直結PCで検証済みの要求と、ビルド前に全64バイト一致を確認した。

受信を一つだけ予約し、正常応答を待つ。長さ・先頭・設定の末尾を検査し、
不一致・送信失敗・1秒の応答期限切れでは故障状態を保持して追加要求を止める。
受信失敗後の自動再送はしない。切断時にも対象情報を無効にする。

実機で確認した結果:

- 設定読取り1回 → 測定値読取り5回 → 設定読取り1回、全7回とも正常な64バイト応答。
- 前後の設定応答は全64バイト同一。
- 測定値5回の応答時間はCM内部で中央値2,063µs、最大2,357µs。
  CMで要求を始めて受信処理へ入るまでの時間であり、PCのCDC往復、転送帯域やDOOMのFPSではない。
- 測定値要求は、純正アプリの一時的なUSBオンライン期限を更新する。
  永続設定変更やFlash操作の要求ではない。

v2 UF2は`artifacts/cm-host-probe-v2/RP2350_CM_HID_QUERY_PROBE_ONLY.uf2`。
SHA-256は`5b801f7bdd43ea3fb81a8471ccc7f46ca0705d7cbf0e83ed10721a2faa37af49`。
これはCM用であり、T3Pに入れるファームではない。

試験後、CMを再起動し、ホスト未開始・入力待機に戻ったことを受信確認した。
その後のスコープ用プログラムへの切替は[波形確認の準備](CM_WAVE_SCOPE.ja.md)を参照。

集約記録は`analysis/cm-usb-bridge.json`。生データは`local/cm-t3p-settings-before.json`、
`local/cm-t3p-measurement-series.json`、`local/cm-t3p-settings-after.json`。

## ビルドと出典

Pico SDK 2.2.0、TinyUSBの固定版、Pico-PIO-USBの
`b186e36bea0f3bcc2ee94ca1fa81e7f71281443a`を使用。
メーカーの古い例のように、TinyUSBの戻り値をコメントアウトする変更は行っていない。
新しいコードはMIT。CDCディスクリプターはPico-PIO-USBの例から取り込み、元のMIT通知を保持。

`scripts/build_cm_host_probe.py`でローカルビルドし、
`scripts/cm_host_probe.py`は既定では状態受信だけ、`--start`で列挙、
`--query settings`または`--query measurement`で固定読取りを行う。

現段階ではUSBストレージクラスや独自SCSI、USB_KEYからUSBホストへの切替を実装していない。
この通常HID通信の成功を、T3Pのバックアップや復旧成功と扱わない。

2026-11-01追記: 新しいオス・メス中継取り出し基板とプローブを挟んだ状態でも、
`0483:5750`・34byteの既知HID記述子が一致し、64byteの設定要求応答が成功した。
記録は`local/cm-inline-t3p-enumeration.json`、`local/cm-inline-t3p-settings.json`。
その後ユーザーがT3Pだけを外し、[一回接続試験](CM_ROM_PROBE.ja.md)の準備へ進んだ。
