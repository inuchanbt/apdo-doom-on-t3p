# ATORCH T3PでDOOM

[English](README.md)

ATORCH T3P本体でFreedoomのE1M1を動かす実験的なネイティブ移植。内蔵240×240 LCDにゲーム画面が出て、左の物理ボタンで発射できた。現在の実機確認版は **V26**。

## これまでの経緯

- まず純正USB HIDで設定や測定値の読取りを確認。左ボタンを押しながら通電すると純正HID更新画面に入る。純正3.0.3の再書込みと通常USBへの復帰は実機で確認済み。T3Pの全Flashバックアップと独立したROM復旧は未確立。
- カラーバー、RGB565と表示範囲の確認、独立した基板初期化の順に進め、E1M1の開始地点とHUDを本体で描画できた。
- 初期の計測は約7.8〜7.9秒／フレーム。V12でDEFLATE復号を高速化し、V13で繰り返し展開が主な負荷だと確認した。V14でフラットと描画列のキャッシュを追加。V16とV20では展開頻度の高いページを選んで無圧縮にし、展開後のゲームデータはバイト単位で維持した。
- V17のDBI DMA試験ではゲーム画面がノイズになった。V18でアクティブ行の設定を修正し、LCD時間はV16の約310〜320 msから134〜138 msへ短縮。V19では4列バッファを2枚にし、描画とDMAを重ねた。
- V20の開始地点付近の2サンプルは **約0.87〜0.93秒／フレーム**。うちDEFLATE展開は約0.59〜0.63秒。左から画面が書き換わる様子はまだ見える。タッチ、傾き、音声、ゲーム操作全体の実機対応は今後の課題。

- V21では120×120の色インデックスで1フレームを保持し、送信時に240×240へ拡大。左からの更新は目立ちにくくなったが、発射時にはまだ見える。V22とV23では頻出5ページを追加で無圧縮化。V23のフレーム時間は**最初1,217 ms、少し経ってから966 ms、発射時1,031 ms**。最初の帯の送信処理から最後のDMA完了までは**162〜163 ms**だった。

- V24でDBI分周セレクターを3から2へ変更し、送信区間は **83〜84 ms** に短縮。発射時の書き換えも目立ちにくくなった。当初の公称8 MHzという見積もりと実測の比率が合わないため、実周波数は未確認として扱う。
- V25では7ページを追加で無圧縮化。初期・静止後・発射時の写真は **1,040 / 704 / 900 ms／フレーム**、展開時間は **642 / 436 / 560 ms**。V26の4バイト単位コピーでは、3枚の写真から明確な改善は確認できなかった。
- **V27は生成・PC検証済みで、実機結果待ち。** 7ビットのハフマン先頭符号テーブルを追加し、V26のコピー変更は無効にした。全1,369ページ、ランダム読取り5,000件、非圧縮ブロックへの遷移を含む420ストリームをPCで検査した。

## フォルダ構成

| フォルダ | 内容 |
|---|---|
| native | 基板起動、LCD/DBI、アセット読取り、エンジン接続 |
| scripts、tests | ビルド、オフライン検査、調査ツール |
| recovery | RP2350補助基板のプログラム |
| docs、analysis | 調査ノートと主要なビルド・実機検証記録 |
| vendor、patches、notices | 取り込んだソース、変更、ライセンス表記 |

メーカー配布物、SDK・ツールチェーン、ゲームパック、書き込み用ファーム、計測の生データはローカル管理。過去の調査ノートには手元のファイルへの参照も残している。

## ローカルビルド

Python 3、指定のJieLi Windows pi32ツールチェーン、手元の純正3.0.3、[SOURCE_LOCK.json](SOURCE_LOCK.json)に記載の変換済みFreedoom 0.13.0 E1M1の DOOM.DAT が必要。変換済みデータは2,802,000バイトで、データ生成には上流／KM003C側の手順を使う。純正ファームは `Atorch/T3P Firmware Version 3.0.3.bin` に置く。

```powershell
$Pi32 = 'C:/path/to/JL/pi32'
$GameData = 'C:/path/to/E1M1/DOOM.DAT'
python vendor/jl-misctools/firmware/fwunpack_newfw.py --dirname "local/unpacked/T3P Firmware Version 3.0.3.bin" "Atorch/T3P Firmware Version 3.0.3.bin"
python scripts/pack_game.py --input $GameData
python scripts/build_engine.py --toolchain $Pi32
python scripts/build_native_board.py --toolchain $Pi32 --game-data $GameData --doom --package-doom --revision 23
```

更新用イメージは `artifacts/native-doom-v23/` に生成される。`DO-NOT-FLASH` の付いたファイルは中間生成物。実機試験前に[復旧の記録](docs/RECOVERY.ja.md)を参照。[V23のビルド記録](analysis/native-doom-v23-build-reference.json)と[ライセンス・出典](THIRD_PARTY_NOTICES.md)も公開している。
