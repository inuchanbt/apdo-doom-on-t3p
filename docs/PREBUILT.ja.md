# ATORCH T3P DOOM V66

[English](PREBUILT.md)

ビルド済みDOOMアプリ、オフライン梱包ツール、PC用ライブキャプチャコントローラーのセット。Freedoom 0.13.0のE1M1をアプリに含む。手元の純正T3P 3.0.3から更新用ファイルを作り、そのブートローダーとリソースを引き継ぐ。純正ファイルは復旧用にも保管する。

配布版V66をT3Pへ書き込み、正常動作を実機確認した。共用HID ID 16c0:05dfを使い、PC側は製造者と製品名を完全一致で照合する。ゲームとPC画面を見ながらのプレイも実機確認済み。[実機記録](../analysis/native-doom-v66-physical-result.json) · [USB IDの条件](USB_ID.md)。

## 更新用ファイルを作る

1. メーカー配布のT3P Firmware Version 3.0.3.binとDcTesterを用意する。
2. t3p-doom-v66-kit.zipを展開し、そのフォルダーでPowerShellを開く。
3. 純正ファイルへのパスを置き換えて実行する。

```powershell
py -3 .\scripts\package_prebuilt.py "C:\path\to\T3P Firmware Version 3.0.3.bin"
```

生成したT3P_NATIVE_DOOM_E1M1_V66_3.0.3.binをDcTesterで使う。ZIP内のDO-NOT-FLASH-t3p-doom-app.binは梱包ツールの入力。純正・アプリ・出力をSHA-256で照合し、既存のファイルへの上書きを拒否する。純正3.0.3のSHA-256は72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2。

## 書き込みと純正への復帰

測定用端子を空け、左ボタンを押したまま側面のPC接続専用HID端子をつないで純正更新画面を開く。DcTesterで生成したファイルを書き込む。測定器へ戻すときは、同じ更新画面で純正3.0.3を書き込む。従来の純正HID復旧経路は実機確認済み。V66から純正への往復は確認待ち。[復旧記録](RECOVERY_DECISION.ja.md)。

## PC画面を見ながら遊ぶ

DOOM動作中に側面HID端子をWindowsへ接続し、ほかのT3Pコントローラーを閉じる。

```powershell
py -3 -m pip install hidapi
py -3 .\scripts\t3p_pc_controller.py
```

[PCコントローラー](http://127.0.0.1:50595/)を開き、キーボードを有効にする。WASD・矢印で移動と旋回、Spaceで発射、Eで扉やスイッチ、Esc・Enterでメニューを操作する。高速60×60が既定で、精細120×120に切り替えられる。プレビューの停止とBMP保存もできる。タブを離れると入力を解除する。

キットだけでも画面枠を表示できる。自分の対応する筐体画像を使う場合は--shell path/to/t3p.pngを指定する。ゼロ入力での認識・往復確認はpy -3 .\scripts\t3p_pc_keyboard.py --probeで行う。PC側は配布版と従来の非公開テスト版の両方に対応する。

## ソースから再ビルドする

t3p-doom-v66-source.zipを展開する。変換ツール、指定版のdoomgenericソース、Freedoom入力ZIP、生成済みの上流テーブルを同梱している。SOURCE_LOCK.jsonにあるJieLi Windows pi32ツールチェーンを別途用意する。

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
py -3 scripts/prepare_lowres_game.py --converter-root ./converter
py -3 scripts/build_engine.py --toolchain $Pi32
py -3 scripts/build_native_board.py --toolchain $Pi32 --doom --revision 66 --release-app --shared-usb
```

artifacts/native-doom-v66-prebuilt-public/DO-NOT-FLASH-app-payload.binができる。release.jsonのpayload_sha256と照合できる。アプリはプロジェクトと取り込んだオープンソースのコードをリンクし、JieLiランタイムアーカイブを含めない。ライセンスと出典はCOPYING、THIRD_PARTY_NOTICES.md、同梱の各ライセンスを参照。
