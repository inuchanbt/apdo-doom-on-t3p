# V45 / V46：低解像度アセットの圧縮／無圧縮比較

2026-11-05作成・実機結果追記。**両版が計測を完走し、全無圧縮のV46で大幅な短縮を確認。**

V44は平均626 ms／フレーム、そのうちDEFLATEが387 ms、表示送信が83 msだった。
今回の目的は、アセットをFlashに無圧縮で収め、反復解凍を不要にすること。

## 変更と比較条件

既存のDMC100変換器を使い、E1M1の壁テクスチャを縦横1/2、スプライトの保存カラムを1/2にする。
元のワールド寸法とパレットは維持し、エンジンが拡大する。モンスターの保存行数とHUDの素材は維持。
HELP1画像を変換器の既存の除外機能で省く。元の変換設定が現行データのSHA-256を再現することも確認。
開始地点のPCプレビューでは大きな破綻は見られないが、近距離での精細さなどは実機で評価する。

- **V45：低解像度・圧縮あり。** V44とバイト単位で同じ解凍オブジェクト。新しい582ページはすべてDEFLATE。
  2 KiB × 35枚のページキャッシュを維持する。
- **V46：同じ低解像度データをすべて無圧縮。** 582ページをFlashから直接読み、最初のアクセス時にCRCを検査する。
  圧縮ページを受け付けず、デコーダー・ページキャッシュ・圧縮ステージング・展開順位の配列を外す。
  解放したRAMは今回は空きとして残し、新たなキャッシュや描画方式には使っていない。

両版の解凍後データは1,189,968 Bで同一。120×120の色インデックス画面、240×240への拡大、DMA表示、
アセット／短レコードの既存キャッシュ、CPU／PLL／Flash／LCD分周設定は共通。
V44は画像の細部が異なるので、記憶方式の効果を分けて測るためにV45を対照とする。

## 実機での手順

1. V45 → V46の順に書き込み、各版を通常起動してボタンを押さずに待つ。
2. 10フレーム準備＋30フレーム測定の後、3行目が `0000D00E` になって止まる。
3. 各版の左上 **全14行** を撮影する。ボタン無効・最後の停止はこの計測版の仕様。

| 行 | 内容 | V45の期待値 | V46の期待値 |
|---|---|---|---|
| 1 | 版（16進） | `0000002D` | `0000002E` |
| 2 | 測定フレーム数 | `0000001E` | `0000001E` |
| 3 | 完了 | `0000D00E` | `0000D00E` |
| 4〜6 | フレーム全体：平均／最短／最長、ms | 実測待ち | 実測待ち |
| 7〜9 | DEFLATE：平均／最短／最長、ms | 実測待ち | すべて0を期待 |
| 10〜12 | 送信：平均／最短／最長、ms | 実測待ち | 実測待ち |
| 13 | 最終tic | `00000028` | `00000028` |
| 14 | 画像列の照合値 | `EBC8D404` | `EBC8D404` |

照合値は従来の `C269CBCE` から変わる。低解像度の画像列をPC上で検証して得た値であり、異常ではない。
固定の1フレーム1 tic・入力なしの試験なので、通常プレイ全般のFPSとは区別する。

## 実機結果

ユーザー提供の2枚で先頭がV45=`0000002D`、V46=`0000002E`、両方とも測定30フレーム・
`D00E`・最終40 tic・照合値`EBC8D404`を確認。各版1回の実測で、ユーザーもV46の明確な高速化を報告。
ここでは版・完了・tic・照合値と平均値を転記し、最短／最長は集計しない。

| 平均値 | V45：圧縮あり | V46：全無圧縮 |
|---|---:|---:|
| フレーム全体 | 1,147 ms (`047B`) | 205 ms (`00CD`) |
| DEFLATE | 864 ms (`0360`) | 0 ms |
| 表示送信 | 99 ms (`0063`) | 99 ms (`0063`) |
| 平均フレーム時間の逆数 | 約0.87フレーム／秒 | 約4.88フレーム／秒 |

同じ低解像度アセットのV45比でフレーム時間は約82.13％短縮、処理速度は約5.60倍。
以前のV44（626 ms）比でも約3.05倍だが、こちらは画質と保存形式の両方が変わる比較。
V45自体はV44より遅いため、低解像度化だけで圧縮版が速くなったとはしない。
アセット・ページの配置や無圧縮選択ページも変わっており、この差の原因は単独で切り分けていない。

V46では205 msのうち送信が99 ms、残り約106 msはゲーム進行・描画・データ読取り等を含む。
現在の`km_present()`は最後のDMA完了を待って戻るため、次フレームの描画とは重なっていない。
解放RAMに2枚目のインデックス画面を置き、次の描画中も前の画面の送信を進める構成が改善候補になる。
これは未実装・未測定で、実際のCPU処理やDMAの制約を含めて確認が必要。

固定1フレーム1 ticの開始地点試験であり、約4.88を通常プレイ全般のFPSとはしない。
入力・移動・他の場面・長時間動作はこの試験の対象外。通常操作の実機確認済み基準は引き続きV30。
元写真はリポジトリーへ保存せず、SHA-256と転記・比較を`analysis/native-doom-v45-v46-benchmark.json`に記録。

## 容量と検証

| 項目 | V45 | V46 |
|---|---:|---:|
| ゲームパック、管理情報込み | 587,058 B | 1,199,344 B |
| 更新ファイル | 1,540,096 B | 1,540,096 B |
| 予約後のFlash残量 | 662,940 B | 54,320 B |
| 静的RAM | 165,756 B | 87,660 B |
| 予約後のRAM残量 | 3,056 B | 81,152 B |

Flashドライバー80 KiB、RAMドライバー32 KiB、スタック16 KiBの予約と、ROM／IRQ・上部512 Bの更新領域を維持。
V46では従来より78,096 BのRAMが空く。全画面の再設計やキャッシュ再配分はこの後の別実験とする。

- 各パックの全582ページ・ランダム5,000範囲・境界／破損検出をネイティブC読取りで検査。
- 無圧縮専用読取りは圧縮パックをopen時に拒否。デコーダーをリンクしない構成でも検査を完走。
- PCの実エンジンで、従来の実機照合値 `C269CBCE` を再現。
- 新しい両版を別プロセスで148ステップずつ実行し、画像とゲーム状態がすべて一致。
  開始地点の固定40 ticに加え、発射・前進・旋回・メニューを通し、各フレームの仮想LCD内容も照合。
- 回帰テスト66件を通過。
- 独立した更新ファイル展開ツールでapp.binとペイロードが一致。app以外の44ファイル・2ディレクトリーも純正と一致。
  先頭20 KiB、リソース末尾と入れ子CRCも作成時に検査。
- V45の解凍オブジェクトはV44と同一。V46のリンク結果には `tinf_uncompress` がない。

書込み用：`artifacts/native-doom-v45/T3P_NATIVE_DOOM_E1M1_V45_3.0.3.bin` と
`artifacts/native-doom-v46/T3P_NATIVE_DOOM_E1M1_V46_3.0.3.bin`。
`DO-NOT-FLASH`付きは中間生成物。

SHA-256：

- V45：`34392ff5033277995c376e26c369642cc0bb055b727a7c1af882e36eec77da80`
- V46：`4aecfeeb8d7feeb9759e14b6f5280ce8c43322c9227b724daca1ec79b28fa423`

## 再作成

```
python scripts/prepare_lowres_game.py --converter-root <full-pinned-dmc100-checkout>
python scripts/build_native_board.py --toolchain <JieLi-pi32-directory> --doom --package-doom --revision 45
python scripts/build_native_board.py --toolchain <JieLi-pi32-directory> --doom --package-doom --revision 46
python scripts/check_game_pack.py --small-pages --prefix-continuation --literal-run --packed artifacts/game-lowres/E1M1-deflate.t3pg --raw artifacts/game-lowres/DOOM.DAT
python scripts/check_game_pack.py --raw-only --packed artifacts/game-lowres/E1M1-all-raw.t3pg --raw artifacts/game-lowres/DOOM.DAT --reject-packed artifacts/game-lowres/E1M1-deflate.t3pg
python scripts/build_engine.py --host-test --game-page-bytes 2048 --fixed-tic-benchmark --output artifacts/host-lowres-deflate
python scripts/build_engine.py --host-test --game-page-bytes 2048 --raw-game-only --fixed-tic-benchmark --output artifacts/host-lowres-raw
python scripts/check_lowres_game.py
```

変換器は公開チェックアウトに含まれるエンジン抜粋とは別に必要。キャッシュ済みFreedoom ZIPと
変換テーブルを使い、ネットからの取得やUSB操作は行わない。各変換器ソースと変換テーブルのハッシュは
`analysis/native-doom-v45-v46-assets.json`、PC実行の照合結果は
`analysis/native-doom-v45-v46-host-check.json`、ファーム記録は各版のbuild-referenceに保存。
上記の速度向上はユーザーの実機写真に基づく。PCでの処理時間をT3PのFPSへ換算しない。
