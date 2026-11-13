# 上游审查检查点

本文件记录**已经完成审查的边界**，供下一轮跟进从这里开始。
更新远端引用不等于完成审查；不要仅因运行了 `git fetch` 就更新本文件。

审查日期：2026-11-13（Asia/Shanghai）。

| 上游分支 | 已审查到的完整 commit hash | 提交标题 |
| --- | --- | --- |
| `upstream/master` | `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6` | Version 0.11.26 |
| `upstream/dev` | `6ec7f0577a1df9bbefdb5236b649268da7665b58` | Fix an error with resource managed stream notifications. |

- 上游仓库：<https://github.com/mackron/miniaudio>，远端名称 `upstream`。
- 自有仓库：<https://github.com/niu2x/miniaudio>，远端名称 `github`。
- 自有长期稳定分支：本地 `niu2x/master`。不要将它与本地 `master` 或远端 `github/master` 混淆。
- 本轮比较起点：`cfb92b4cbb7c20b23a2af1527524ca9b28917bfb`。
- 本轮修复与测试完成时的本地提交：`6da884572dc2193bb3a57135527ba552aec7c071`。
- 本轮指定的 9 个修复已合入本地；截至记录时尚未推送远端。
- “已审查”包含已合入、已覆盖以及明确跳过的提交，不表示这两个上游分支已整体合并。

下一轮操作方法、提交处理清单和验证范围见
[上游维护交接指南](upstream-maintenance-guide.md)。
