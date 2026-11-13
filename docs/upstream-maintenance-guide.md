# 上游维护交接指南

## 维护目标与分支关系

`niu2x/master` 是我们自己的长期稳定分支。预期不再引入大功能，持续维护自有
bugfix，并选择性同步上游对现存功能的 bugfix。不要直接整体合并上游 dev。

| 远端 | 仓库 | 用途 |
| --- | --- | --- |
| `upstream` | `https://github.com/mackron/miniaudio.git` | 上游源码 |
| `github` | `git@github.com:niu2x/miniaudio.git` | 我们自己的远程仓库 |

维护目标是本地 `niu2x/master`；本地 `master`、`github/master` 和
`upstream/master` 是不同引用。推送前应明确目的远端和目的分支，不要凭名称推断。
本轮仅完成本地合入和提交，没有推送。

## 本轮审查边界与结果

审查日期为 2026-11-13（Asia/Shanghai）。两条上游分支的完整审查边界单独保存在
[上游审查检查点](upstream-review-checkpoint.md)，下一轮以该文件为起点。

本轮基线是 `cfb92b4c`。`upstream/master` 没有新增提交；`upstream/dev` 相对
本轮基线有 14 个尚未包含的提交。逐项筛选后合入了下面 9 个修复，均使用
`git cherry-pick -x` 保留上游来源，没有带入 fs、JACK 或贡献指南更新。

| 上游提交 | 本地提交 | 修复内容 |
| --- | --- | --- |
| `145ea6e3` | `37c7eb04` | 内置 WAV 元数据边界、FLAC seek 零分母路径、MP3 元数据长度及音频起始偏移 |
| `ce8acb36` | `7092af72` | 显式编码格式下的自定义 decoder 被拒绝，以及失败路径的 VFS 文件句柄泄漏 |
| `0822f150` | `06cc92ba` | WASAPI 设备格式查询的内存泄漏 |
| `e47d7600` | `40de172c` | sound seek 后清理缓存与重采样历史，seek 失败时保留节点时间 |
| `a99679e4` | `9fdd39fd` | engine 初始化失败或设备启动失败时清理资源管理器 |
| `ed3c4775` | `2516dceb` | AAudio 重路由保留 performance profile |
| `acde317a` | `9749e833` | WASAPI 重路由失败后的客户端空指针及错误重启路径 |
| `f48b4a37` | `e8c2cc62` | 异步资源释放与后续页解码任务投递的竞态 |
| `6ec7f057` | `6aebf050` | stream 不应获取无法完成的 done fence |

回归测试及平台审查记录提交为 `6da88457`。截至该提交工作区干净。
文档提交可以位于它之后，因此不要把这个提交误认为后续永远不变的分支 HEAD。

其余 5 个上游提交的处理决定：

| 上游提交 | 决定与原因 |
| --- | --- |
| `fda5b20a` | 已被自有 `cfb92b4c` 等价覆盖：注销失败时释放数据缓冲区树锁。保留自有回归测试，不重复合入。 |
| `631161e4` | 跳过整包 fs 更新，包含 API 签名变化和较大重构。未来如有具体需求，应单独拆审 bugfix。 |
| `011b539e` | 暂缓 JACK 错误静默化。它修改进程级错误回调，可能屏蔽宿主其他模块的 JACK 错误输出。 |
| `54f426d4` | 跳过贡献指南更新，与稳定分支运行时修复无关。 |
| `a2d9f7e1` | 跳过纯空白调整。 |

这些跳过的提交已经审查过。除非出现新的需求或证据，不必在下一轮重新作为
“遗漏修复”处理。上游 master 以后可能包含我们已经从 dev 挑选的补丁，也应查重。

## 已完成的验证及限制

本机为 macOS arm64。普通构建启用 warnings-as-errors；conversion、filtering、
generation、resource_manager、新增 maintenance，以及 C/C++ 设备测试共 7 项通过。
设备测试实际使用 Null 后端，不能据此宣称已验证真实音频硬件。
generation 首次使用 60 秒时限超时，单独以 240 秒时限重跑通过，耗时约 48 秒。

另外完成：

- ASan/UBSan 下的 maintenance、resource_manager、conversion、filtering 测试通过。
- 新增 maintenance 测试以 C89、C++11 编译并运行通过，均启用 `-Wall -Wextra -Werror`。
- C89 下分别禁用 device I/O、resource manager、engine、decoding，以及同时禁用
  device I/O 和 threading 的语法编译检查通过。
- WAV、decoder 清理、engine 分配清理、seek、页解码取消、stream fence 共 6 组
  回归在合入前 `cfb92b4c` 源码上复现失败，在合入后通过。
- 异步资源测试包含可控的“页读取过程中注销”顺序，以及 4 个工作线程下 500 轮
  加载/释放，交替覆盖 WAIT_INIT 和已知/未知长度。

Windows WASAPI 和 Android AAudio 没有在对应平台编译或运行；已审查资源所有权、
错误返回、锁释放、重路由状态和配置保留。未发现这 3 个补丁新增的阻断问题。
AAudio 改动涉及设备结构体布局，库和使用方需要一起重新编译。

更多测试细节、平台审查结论和生成的 MP3 样本来源见
[maintenance 测试说明](../tests/maintenance/README.md)。FLAC/MP3 测试覆盖正常
解码及 seek，不代表穷尽异常输入；并发压力测试也不代表所有调度组合均已验证。

## 下一轮跟进步骤

1. 阅读本指南和检查点，检查 `git status`、当前分支、远端 URL 以及本地维护分支
   的新提交。保留已有自有修改；不要为跟进上游而重置工作区或稳定分支。
2. 运行 `git fetch upstream`，记录新获得的两条分支 HEAD。先检查旧审查边界是否
   仍为新 HEAD 的祖先；如果上游改写历史，不要直接用普通范围日志判断全部增量。
3. 分别审查两个分支自旧边界以来的提交。下面命令使用本轮记录的完整边界；
   以后的轮次应换成当时检查点中的值。

   ```sh
   git merge-base --is-ancestor 877ea4a7c2601c7b0f2a44e83dfac42e6470fea6 upstream/master
   git merge-base --is-ancestor 6ec7f0577a1df9bbefdb5236b649268da7665b58 upstream/dev
   git log --reverse --oneline 877ea4a7c2601c7b0f2a44e83dfac42e6470fea6..upstream/master
   git log --reverse --oneline 6ec7f0577a1df9bbefdb5236b649268da7665b58..upstream/dev
   ```

4. 对候选修复阅读实际补丁及完整调用链，确认触发条件、影响平台、依赖、失败
   清理、锁和任务顺序、API/ABI 变化。依赖更新即使标题普通，也可能包含重要
   bugfix；同样，标题含 fix 也不代表适合稳定分支。对合并提交检查实际引入的
   改动，避免只看提交标题。
5. 用提交来源、补丁内容和当前代码确认是否已覆盖。可辅助运行
   `git cherry niu2x/master upstream/master` 和
   `git cherry niu2x/master upstream/dev`，但补丁等价检查不能替代人工判断。
   特别注意我们自己的修复、从另一个分支已挑选的修复，以及本轮明确跳过的项。
6. 在用户授权的合入范围内选择修复并使用 `git cherry-pick -x`。优先保持原提交
   边界；如需适配或额外修复，明确记录理由及与原补丁的差异。不要顺带带入大功能。
7. 本地可测路径运行相关测试；对并发、泄漏、缓存、错误路径补有意义的回归，
   尽可能验证旧版本失败、新版本通过。不能本地运行的平台仔细审查并明确说明
   尚未验证的范围，不能把 Null 后端或宏裁剪编译当成该平台验证。
8. 完成审查后更新检查点和本指南：记录实际已审查到的完整 hash、日期、候选
   提交的合入/已覆盖/跳过决定、测试结果、遗留问题及本地/远端同步状态。
   如果只审查了部分增量，不要把整个分支的新 HEAD 标为已完成审查。

## 常用验证命令

以下为本轮已使用的构建方式。测试输出放在仓库外；路径可按环境调整。

```sh
cmake -S . -B /private/tmp/miniaudio-maintenance-build \
  -DMINIAUDIO_BUILD_TESTS=ON -DMINIAUDIO_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/miniaudio-maintenance-build -j 4
ctest --test-dir /private/tmp/miniaudio-maintenance-build --output-on-failure --timeout 240
```

设备测试应查看实际使用的后端。需要明确使用 Null 时，可单独执行：

```sh
/private/tmp/miniaudio-maintenance-build/miniaudio_deviceio null --auto
/private/tmp/miniaudio-maintenance-build/miniaudio_cpp null --auto
```

```sh
cmake -S . -B /private/tmp/miniaudio-maintenance-asan \
  -DMINIAUDIO_BUILD_TESTS=ON -DMINIAUDIO_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build /private/tmp/miniaudio-maintenance-asan \
  --target miniaudio_maintenance miniaudio_resource_manager miniaudio_conversion miniaudio_filtering -j 4
ctest --test-dir /private/tmp/miniaudio-maintenance-asan \
  -R 'maintenance|resource_manager|conversion|filtering' --output-on-failure --timeout 60
```
