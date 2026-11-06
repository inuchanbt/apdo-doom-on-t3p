# ATORCH T3P DOOM V58.1

[English](PREBUILT.md)

ビルド済みDOOMアプリと、手元の純正T3P 3.0.3から更新用 `.bin` を作る梱包ツールのセット。Freedoom 0.13.0のE1M1と起動ロゴはアプリに含まれる。梱包時に必要なのはPython 3と純正ファイル。梱包ツールはオフラインで動作する。

V58.1はV58のプレイ構成を使い、気圧計算の64ビット除算をプロジェクト内の実装でリンクした版。V58.1の起動、操作、純正への復帰は実機確認待ち。

## 更新ファイルを作る

1. メーカー配布の `T3P Firmware Version 3.0.3.bin` とWindows用のDcTesterを用意する。純正 `.bin` は復旧用にも別途保管する。T3P専用で、T3や別バージョンのファイルはハッシュ照合で弾かれる。
2. `t3p-doom-v58.1-kit.zip` を展開し、そのフォルダーでPowerShellを開く。
3. 次のコマンドで更新用イメージを作る。純正ファイルへのパスは実際の置き場所に合わせる。

```powershell
py -3 .\scripts\package_prebuilt.py "C:\path\to\T3P Firmware Version 3.0.3.bin"
```

生成される `T3P_NATIVE_DOOM_E1M1_V58.1_3.0.3.bin` をDcTesterで使う。ZIP内の `DO-NOT-FLASH-t3p-doom-app.bin` は梱包用。ツールは純正・アプリ・出力をそれぞれSHA-256で照合し、既存ファイルへの上書きを拒否する。純正3.0.3のSHA-256は `72571d747e9da86194deddae3802992f846415c45b82d7b28f8aa567af30acb2`。

## T3Pへの書き込みと復旧

測定用端子を空け、左ボタンを押したままPC接続専用HID端子をつないで純正更新画面を開く。DcTesterで上で生成した `.bin` を選ぶ。DOOMの間は純正の測定機能が使えない。測定器に戻すには同じ手順で純正3.0.3を書き込む。純正HIDでの復旧は以前の実機で確認済み。全Flashの独立復旧は未確立。[実機での純正復旧記録](RECOVERY_DECISION.ja.md)

対応ソースは `t3p-doom-v58.1-source.zip` に同梱する。ビルド手順は[英語版](PREBUILT.md)を参照。
