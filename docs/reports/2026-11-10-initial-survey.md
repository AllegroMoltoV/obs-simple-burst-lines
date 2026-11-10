# 初回セットアップの調査

## はじめに

`.prompts/INIT.md` の要件の文書が未確認として残した事項と、骨組みを作る前に知る必要のある事項を調べた記録です。要件の文書に書かれた調査結果は繰り返しません。調査日は 2026-11-10 です。

## 調査方法

調べた範囲と方法は次のとおりです。

| 対象 | 方法 |
| --- | --- |
| 名前の重複 | `gh repo view` と、倉庫くんの MCP の `search_plugins` |
| OBS 本体の表示と非表示の機能 | `D:/work/obs-simple-drop-shadow/.deps/obs-studio-31.1.1/` のソースと、obs-studio の master のソースをサブエージェントが読んだ |
| drop-shadow の流用元 | `D:/work/obs-simple-drop-shadow` の `git ls-files`、`CMakeLists.txt`、`tests/` の grep |
| 手元のビルドと動作確認の環境 | `cmake --version`、OBS の実行ファイルの版、`.deps` の内容 |

## 調査結果

### 名前の重複

GitHub の `AllegroMoltoV/obs-simple-burst-lines` は存在しません。`gh repo view` が `Could not resolve to a Repository` を返しました。

倉庫くんの `search_plugins` では、`burst`・`集中線`・`lines` のいずれも 0 件でした。対照として `drop shadow` で検索すると obs-simple-drop-shadow が 1 件返ったため、検索そのものは機能しています。

### OBS 本体の表示と非表示の機能

OBS 31.1.1 と master のソースで確認しました。OBS 30.x・32.x・33.x のタグは個別に確認していません。実機の設定画面も確認していません。

シーンのアイテムごとの表示と非表示のホットキーがあります。`libobs/obs-scene.c` の `init_hotkeys()` が、アイテムごとに `libobs.show_scene_item.<item id>` と `libobs.hide_scene_item.<item id>` を `obs_hotkey_pair_register_source()` で登録しています。登録者はシーンのソースです。そのため設定画面では、シーン名の見出しの下に表示されると読めます。

シーンのアイテムごとの表示と非表示のトランジションもあります。API は `libobs/obs.h` の `obs_sceneitem_set_transition` などです。`obs_sceneitem_set_visible()` が内部でトランジションを呼ぶため、ホットキーで切り替えたときもトランジションが適用されます。公開版の文書は https://docs.obsproject.com/reference-scenes です。

ソースが `obs_hotkey_register_source()` で登録したホットキーは、`frontend/settings/OBSBasicSettings.cpp` の列挙の処理により、ソース名の見出しの下に表示されると読めます。フィルターを除外する処理は見つかりませんでした。実機では確かめていません。

`obs_source_info` には `activate`・`deactivate`・`show`・`hide` のコールバックがあります。

### drop-shadow の流用元

drop-shadow が git で管理するファイルは 113 件です。テンプレート由来の `cmake/`・`build-aux/`・`.github/` と、`src/`・`data/`・`tests/`・`docs/`・`.soko-kun/` からなります。

`docs/` と `README.md` を除き、drop-shadow の名前を含むファイルは 13 件です。

| ファイル | 件数 |
| --- | --- |
| `src/drop-shadow-filter.cpp` | 30 |
| `.github/scripts/Test-ObsLoad-Windows.ps1` | 19 |
| `CMakeLists.txt` | 10 |
| `.github/workflows/render-test.yaml` | 10 |
| `tests/install-probe.cpp` | 9 |
| `src/item-compensation.cpp` | 4 |
| `tests/filter-benchmark.cpp` | 4 |
| `src/plugin-main.cpp` | 2 |
| `tests/filter-integration-test.cpp` | 2 |
| `buildspec.json` | 1 |
| `data/locale/en-US.ini` | 1 |
| `data/locale/ja-JP.ini` | 1 |
| `tests/filter-render-fixture.hpp` | 1 |

drop-shadow のテストは、テスト用の入力ソースを作ってフィルターを付け、`gs_texrender` に描いて画素を読む形です。`CMakeLists.txt` は、統合テストを D3D11 と OpenGL の両方で CTest に登録します。グラフィックスの DLL とデータは `.deps/obs-studio-31.1.1/` の下を参照します。

### 手元の環境

| 項目 | 値 |
| --- | --- |
| CMake | 4.2.1 |
| Visual Studio | `C:/Program Files/Microsoft Visual Studio/` の下に `18` と `2022` がある |
| OBS Studio | 33.0.1。`C:/Program Files/obs-studio/bin/64bit/obs64.exe` の製品の版 |
| OBS 31.1.1 のビルド済みのグラフィックスの DLL | drop-shadow の `.deps/obs-studio-31.1.1/build_x64/` に `libobs-d3d11.dll` と `libobs-opengl.dll` がある |

## 考察

名前は GitHub と倉庫くんのどちらとも重複しないため、U2 の決定のまま着手できると考えています。

入力ソースでは、表示と非表示の切替えに OBS 本体のアイテムごとのホットキーとトランジションを使えると考えています。プラグインに独自のホットキーやフェードを持たせる必要は無いと推測します。ただし OBS 33.0.1 の実機では確かめていないため、試作の確認のときに依頼者に確かめてもらいます。

テストの構成は、フィルターを付ける手順を除けば入力ソースへ転用できると考えています。`.deps` は drop-shadow のものを複製すれば、OBS 31.1.1 の再ビルドを省けると推測します。
