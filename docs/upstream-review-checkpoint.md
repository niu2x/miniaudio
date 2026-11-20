# 上游审查检查点

本文件记录**已经完成审查的边界**，供下一轮跟进从这里开始。
更新远端引用不等于完成审查；不要仅因运行了 `git fetch` 就更新本文件。

审查日期：2026-11-20（Asia/Shanghai）。

| 上游分支 | 已审查到的完整 commit hash | 提交标题 |
| --- | --- | --- |
| `upstream/master` | `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6` | Version 0.11.26 |
| `upstream/dev` | `65c1fd32a5b89aa821eb4c50af189deacb5b7062` | Update amalgamation. |

- 上游仓库：<https://github.com/mackron/miniaudio>，远端名称 `upstream`。
- 自有仓库：<https://github.com/niu2x/miniaudio>，远端名称 `github`。
- 自有长期稳定分支：本地 `niu2x/master`。不要将它与本地 `master` 或远端 `github/master` 混淆。
- 本轮比较起点：`756e2effbe2a732daad772cf9d7fab70f3e04464`。
- 本轮修复与测试完成时的本地提交：`e6fc43d9641c44e608ea91c6da9d4d7fb0d7178e`。
- 本轮从 `65c1fd32` 部分迁移解码器 bugfix；排除 Lyrics3v2 新功能及两处纯优化。此前 2026-11-13 的 9 个修复和自有 OpenSL 停止修复均保留；本次迁移尚未推送远端。
- 增量中的 fs 更新 `9e6360f2`、`c794d523` 已审查并跳过：属于已暂缓的 fs 升级中的 DOS/DJGPP 支持。
- “已审查”包含已合入、已覆盖以及明确跳过的提交，不表示这两个上游分支已整体合并。

下一轮操作方法、提交处理清单和验证范围见
[上游维护交接指南](upstream-maintenance-guide.md)。
