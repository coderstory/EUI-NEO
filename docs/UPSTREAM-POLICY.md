# 上游策略（fork-and-freeze）

本文件记录本仓库相对上游 sudoevolve/EUI-NEO 的跟进策略：**fork-and-freeze——自 fork 点起不再跟随上游，main 分支按下游消费方（DevDesk）的需求独立演进**。策略成文于 2026-10-26（此前只是协作中的事实认知，本文将其升为成文决策）；下游视角的对应决策见 DevDesk 仓 `docs/adr/0007-eui-neo-upstream-fork-and-freeze.md`。

## 事实基础

以下数字为 2026-10-26 实测（`git log -1 --format='%h %ad %s' --date=short upstream/main` 等，复核方法同源可重复）：

| 项目 | 事实 |
|---|---|
| 上游仓库 | `sudoevolve/EUI-NEO`（remote 名 `upstream`） |
| fork 点 | `782c569`（2026-09-22，「Merge branch 'dev'」）——即上游 main 的停驻处 |
| 分叉起点 | `bb5c965`（2026-09-22，fork 侧首个本地提交） |
| 上游活动 | 截至 2026-10-26，上游 main 停在 fork 点已 **34 天（约 5 周），分叉后零活动** |
| fork 领先 | 本地 main（= `origin/main`，coderstory fork）领先 `upstream/main` **125 个提交**（随开发持续增长，不为计数设锚） |

## 决策：fork-and-freeze

**不再跟随上游。** main 是本 fork 的唯一主线，按下游消费方的实际需求直接演进；不维护与上游的例行同步通道（不做周期性 merge / rebase `upstream/main`）。

理由：

1. **上游已休眠**：上游 main 自 2026-09-22 起零活动，「跟随上游」当前没有跟随对象，只剩跟随成本——分叉面已 125+ 提交，每拖一周，将来若真要合并，冲突面只增不减。
2. **演进由下游需求驱动**：本 fork 的首要（当前唯一）消费方是 DevDesk（GUI 应用）。框架能力按其需求增补（如 `setScrollOffset`、`TextRun`、chromeless 等 API 均为 fork 侧新增、上游没有），上游并无可「跟进」的能力源。
3. **rev 对应关系本就不可依赖**：本仓历史上整体重写过 git 历史，与上游的提交号对应关系已断；任何「与上游对齐到某个 rev」的机制在本仓没有稳定锚点。

## 与 DevDesk 消费政策的关系

DevDesk 仓的 ADR 0006（`DevDesk/docs/adr/0006-eui-neo-consume-policy-latest-source.md`）规定了消费侧口径：**同级源码目录引入、不锁版本、每次用最新源码**；版本匹配的唯一权威判据是内容级守卫 `DevDesk/scripts/check-eui-neo-version.sh`。本策略与之互为表里、口径一致：

- 消费政策里的「最新源码」指 **fork 主线（本仓 main = `origin/main`）最新**，不是 upstream 最新；
- DevDesk CI 的取框架步骤 `git clone --depth 1 <fork>` 拉到的就是 fork HEAD（DevDesk 仓 `.github/workflows/ci.yml`）；
- 若有一天本仓手动合并了上游提交，消费方无感、机制零改动——「最新」的定义始终是 fork 主线 tip，API 兼容仍由内容级守卫在构建前兜底。

## 重估条款

**重估信号：`upstream/main` 出现新提交（即上游复活）。**

信号出现后**不自动恢复跟随**，而是重开一次决策，逐项评估：

1. 上游新提交里有什么值得要的（修复 / 能力 / 许可证变化）；
2. 获取方式：cherry-pick 指定提交，还是整体 merge / rebase `upstream/main`（125+ 提交的分叉面上 rebase 成本需实测后再定）；
3. 若结论仍是继续 freeze，记录当次评估结论，本条款顺延。

信号出现之前，本策略不需要任何日常维护动作。
