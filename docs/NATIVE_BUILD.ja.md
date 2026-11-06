# AC707N向けのオフラインビルド

現在のゲーム用ファームの手順は [V58のローカルビルド](../README.ja.md#v58のローカルビルド) を参照。
2026-11-03追記：別の純正起動を利用したネイティブ描画試験は実機で成功。
可視領域240×240・原点0,0・RGB565の色順を確認したため、DOOMのPC検証も240を既定にした。
以下の容量検査ELFに実起動処理や実ドライバーが追加されたわけではない。
別のCPU/RAM初期化・厳しい容量制限を持つ検査ELFも追加した。
[起動・配置の検査](NATIVE_RUNTIME.ja.md)参照。そちらも基板処理は仮実装で書込み不可。

これは実機起動前の容量検査。検査ELFには圧縮ゲームと読取り処理を含むが、
実機の起動、時計、LCDの電源・初期化と通信、USB、GPIO/I2Cは実装していない。
列描画から液晶向けコマンドへの変換は含むが、検査用の出力先へ送るだけである。
`DO-NOT-FLASH-linkcheck.elf` を更新用に梱包したり、本体へ書き込んだりしない。

公式SDKは`local/jieli-sdk`に必要部分だけ取得した。リビジョンは`SOURCE_LOCK.json`参照。
SDK全体の既定ビルドは更新処理を呼び得るため、この作業では実行していない。

公式Windowsツールチェーンは[配布リンク](https://pkgman.jieliapp.com/s/win-toolchain)から取得。
今回の2.5.3インストーラーのSHA-256:
`399bd518a28e47a7a0394506cebd5bf658748a58cd6b7c02512e24b4b88c5a5e`。
インストーラーを起動せずinnoextract 1.9で展開し、以下の場所を使用した。

```text
local/tools/jieli/C$/JL/pi32/bin/clang.exe
local/tools/jieli/C$/JL/pi32/bin/pi32v2-ld.exe
local/tools/jieli/C$/JL/pi32/pi32v2-lib/r3-large/
```

この`C$`は展開先のディレクトリ名。管理共有へのアクセスではない。

```powershell
Set-Location D:\doom_on_t3p
$T3pPython = 'D:\KM003C\.venv\Scripts\python.exe'
& $T3pPython scripts\check_input.py
& $T3pPython scripts\check_display.py
& $T3pPython scripts\pack_game.py
& $T3pPython scripts\check_game_pack.py
& $T3pPython scripts\build_engine.py
& $T3pPython scripts\link_engine_probe.py
```

ビルドは`-target pi32v2 -mcpu=r3 -mllvm -pi32v2-large-program=true`を使用。
既存低メモリDOOMを引き継ぎ、4KiBデータキャッシュ、32KiBのエンジン用領域、
4KiB×8のゲーム展開キャッシュを含む。
`native/doom_bridge.c`などの`km_`という記号名は既存エンジンとの互換性のため残した。
このフォルダーのvendorソースには既存パッチが適用済み。`patches/dmc100-doom.patch`を再適用しない。

`build.json`はコンパイラー／アーカイブのハッシュ、87ソースそれぞれの領域容量を記録。
`linkcheck.json`はリンク後の静的容量、`linkcheck.map`は配置、`linkcheck.log`はリンカー出力。
リンカーは必要なシンボルを解決した。ELFは32bit little-endian、Machine 0xF1。
Windowsの制限された実行環境では旧公式リンカーがDLL再配置エラーになることがある。
今回、通常のWindows実行権限で同じリンカーの正常終了を確認した。

入力・センサー検査は別のWindows用コンパイラーで実行する。
`check_input.py --compiler <clang.exe>`でパスを変更できる。仮データのみを渡し、
入力遷移、ボタンのチャタリング、時計の桁あふれ、タッチ範囲、方向の競合、
センサーの符号・ビット配置、短い応答・通信失敗を検査する。実機動作の代替にはならない。

既存Freedoom変換パック2,802,000バイトを圧縮し、1,189,607バイトにした。
これを検査ELFの専用.gameセクションにも埋め込んだ。
実起動用ファームの配置や復旧は未完成。[圧縮形式と検査](COMPRESSED_GAME.ja.md)参照。

PC用の検証:

```powershell
& $T3pPython scripts\build_engine.py --host-test
& $T3pPython scripts\check_simulator.py
& $T3pPython scripts\check_simulator.py --panel 240
```

Windows用DLLに同じDOOMと入力・展開処理を入れ、仮の時計・LCD・センサー入力で動かす。
ゲーム起動、140フレーム、傾き操作、射撃、2ボタンのメニュー操作を確認した。
`artifacts/host-engine/simulator-check.json`と`windows-simulator.png`が結果。
画面変換にPillowを使うため、この検査の画像出力にはPillowも必要。
実機の表示サイズ・向き・性能を示すものではない。
液晶コマンドの仮想パネル検査と最新の容量は[液晶出力](DISPLAY.ja.md)参照。
