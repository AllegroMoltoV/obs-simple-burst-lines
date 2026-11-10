# 文書索引

context compaction で文脈が失われた場合は、この索引から必要な文書をたどる。

| 知りたいこと | 参照先 |
|---|---|
| プロジェクトの目的と使い方 | `README.md` |
| 要件と初回セットアップの指示 | `.prompts/INIT.md` |
| 調査レポート | `docs/reports/` |
| 名前の重複、OBS 本体の表示と非表示の機能、drop-shadow の流用元、手元の環境 | `docs/reports/2026-11-10-initial-survey.md` |
| 依頼者のユーザーテストのフィードバックの記録 | `docs/reports/2026-11-10-user-test-feedback.md` |
| 描画の統合テストの内容と結果、1920×1080 の GPU 時間の測定値 | `docs/reports/2026-11-10-render-tests-and-gpu-time.md` |
| 1.0.0 の公開記録 (GitHub のリポジトリとリリース、倉庫くんへの掲載) | `docs/reports/2026-11-10-release-1.0.0.md` |
| 依頼者向けの手順書 (git 管理外) | `.prompts/MANUALS/` |
| ロゴの元の SVG と 512×512 の PNG | `docs/images/logo.svg`、`docs/images/logo.png` |
| 操作の様子の GIF。依頼者が ScreenToGif で収録し、幅 640 px・32 色に縮めたもの | `docs/images/obs-simple-burst-lines.gif` |
| 実行ログ (生の出力。git 追跡対象外) | `.logs/` |
| 実行ログ (残す価値があると判断したもの) | `docs/logs/` |
| 計画と決定事項 (Beads 使用時の正本) | `bd show <親課題>` |
| 計画 (Beads 非使用時) | `.prompts/PLANS/` |
| 判断を要する論点 (Beads 非使用時) | `.prompts/DISCUSSIONS/` |
| タスクの状態、依存関係、ブロッカー | `bd ready` / `bd blocked` / `bd list` |
