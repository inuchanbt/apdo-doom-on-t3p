# ネイティブ起動・メモリ配置の準備

同日追記：[独立起動・基板処理](NATIVE_BOARD.ja.md)で実MMIOの時計・LCD処理を接続し、
診断候補とDOOM結合ELFを作成した。以下はその前段階の配置検査の記録。
現在のCPU入口にはRAM初期化前の起動情報保存も追加されている。

2026-11-03。CPU向けの入口、Cの初期値コピー・ゼロ初期化、実アプリ容量内の
配置検査を追加した。独立した基板初期化は未完成で、実機起動を示す成果物ではない。
`artifacts/pi32v2-engine/runtime-layout/DO-NOT-FLASH-runtime-layout.elf`は
時計・表示・入力が検査用の仮実装のままなので、更新用に梱包・書込みしない。
現在実機で動いているのは、成功済みのRGB/表示範囲試験ファームのまま。

## 今回の変更

- `native/runtime_start.S`は割込みを止め、SP/SSPを予約スタック上端へ設定。
  R0〜R2と割込み状態をゼロ初期化しない16バイトへ保存し、RAM初期化から基板入口へ進む。
  基板入口はこの検査では仮処理。ウォッチドッグ、電源、クロック、SFC、キャッシュ、
  割込み表の実設定は含まない。保存するのはレジスター値のみで、渡されたポインター先の
  boot_info等はまだ退避していない。RAM初期化前の実ハンドオフ処理は今後必要。
- `native/runtime_memory.c`は4バイト単位のコピー・ゼロ初期化を実装。
  その処理でSDKのROM関数や未初期化のlibcへ依存しない。
  コピー元・先・長さの整列はリンカーと独立したELF検査で照合。
- `native/runtime_layout.ld`は正式3.0.3の既存app.binと同じ1,465,664バイトへ制限。
  ROM/IRQ用の下位RAMと、上端512バイトの更新用RAMを除外した。
  PSRAMやキャッシュ領域のRAM転用は容量に算入していない。
- `native/t3p_display.c`は、既存の2A/2B/2Cコマンド経路に加え、
  実機LCD試験と同じRGB565の16ビット画素と矩形座標を渡す経路を追加。
  生のSPIバイト列と純正の画素引数を混同しない。8列分3,840バイトの
  バッファを共用し、2つの経路のどちらか一方だけを指定する。
  コールバックはDMAを含む転送完了後に戻る必要がある。
  実際の純正関数呼出し・LCD初期化・転送タイムアウトはまだ接続していない。

## 容量検査

| 項目 | バイト |
| --- | ---: |
| ゲームデータ込みコード・初期値など | 1,318,464 |
| ドライバー用Flash予約 | 98,304 |
| アプリ領域の残り | 48,896 |
| 初期値・BSS・起動引数の静的RAM | 89,428 |
| ドライバー用RAM予約 | 32,768 |
| スタック予約 | 16,384 |
| 整列後の未割当RAM | 79,392 |

予約は完成したドライバーの実測容量ではない。スタックの最大使用量、実処理速度も未測定。
他バージョンの小さいapp.bin領域に収まることは主張しない。

## 検査結果と再現

```powershell
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\check_display.py
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\build_engine.py --host-test
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\check_simulator.py
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\check_simulator.py --transport native-rgb
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\build_engine.py
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\link_engine_probe.py --runtime-layout
& 'D:\KM003C\.venv\Scripts\python.exe' scripts\check_runtime_layout.py
```

Windows環境の制限により公式リンカーがDLL再配置エラーになる場合、通常の実行権限が必要。
これらのスクリプトはUSB・更新ソフトへ接続しない。

- 既存コマンド経路と新RGB565矩形経路の両方で、端・部分バッチ・逆順・重なり・
  不正引数・転送失敗後の停止を検査し一致。
- 両経路でPC上の同じDOOMを175ステップ実行。140フレーム描画と傾き・射撃・
  2ボタンメニューを確認。各ステップの全画素と外周が一致し、両方5,099矩形。
  新経路の記録は `artifacts/host-engine/native-rgb/simulator-check.json`。
- RAMコピー・ゼロ初期化は長さ0〜1,024ワードと前後の番兵を検査。
- 正常ELFの配置を検査し、故意に巨大なRAM/Flash入力を追加したリンクは拒否。
  `artifacts/pi32v2-engine/runtime-layout/checks.json`に記録。

## 公式SDKから取得した最小調査材料

`scripts/prepare_sdk_runtime.py`は、SOURCE_LOCK.jsonの固定リビジョンから
cpu.a・common_lib.a・device.aをハッシュ照合してローカルに取得。
起動・クロック・SPI・GPIO・ウォッチドッグ・タイマーの6メンバーを取り出し、
LLVM中間形式のものを同じpi32v2/r3へ変換して、未解決の依存先を一覧化する。
SDKの既定ビルド・更新処理を実行せず、このライブラリを実機用としてリンクもしていない。
`analysis/sdk-runtime-drivers.json`に指紋と依存先を記録。SDKはローカル調査用とし、
一括した再配布許諾があるとは扱わない。

SDK付属rom.imageは実機ROMの読み戻しではない。maskrom_stubs.ldの番地をそのまま
採用せず、実機に入れる前に対象ROM・純正コードとのABI照合が必要。

次の実装対象は、boot_info/SFCの引継ぎ、ウォッチドッグ・時間源・クロック、
LCDの電源・初期化・転送完了を扱う最小の基板処理。
これらと実機入力を接続するまでは、独立したDOOM起動は未達。
