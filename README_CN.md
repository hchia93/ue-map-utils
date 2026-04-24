# ue-map-utils

[English](README.md) | **中文**

![Claude Code](https://img.shields.io/badge/Claude_Code-black?style=flat&logo=anthropic&logoColor=white)
![Unreal Engine 5](https://img.shields.io/badge/Unreal_Engine-5.7-blue?logo=unrealengine&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

Level Designer 工作流插件：在 UE Editor 内做关卡内资产完整性审计、session 内改动追溯，以及给 AI 工具链准备的上下文导出。

## 它解决什么问题

在 UE 关卡里做 whitebox / blockout / 维护的 LD 工作流中，以下场景反复出现：

- **StaticMeshActor 上的 mesh ref 被 null-out**：资产被删、merge 冲突、redirector 丢失，关卡还能打开但东西消失了，playtest / QA 前没人发现
- **"我不记得我这一轮改了什么"**：一个 editor session 里动了几十个 actor，Ctrl+S 前想核对一下动了什么、属性具体怎么变的，UE 原生没有直接入口
- **UE 原生 Map Check (F7) 每张图手动**：只看当前打开的 level，结果不聚合，LD 不会主动开

这些场景的共性：问题发生在 **关卡内 Actor 层面**（不是资产本身），需要一个 Editor 内的、有交互面板的工具箱。

## 它如何解决

插件是一个 Editor-only UE Plugin，在 Level Editor 的 Window 菜单下暴露一个可 dock 的 Slate 面板。

当前稳定可用的两块功能：

- **Audit StaticMesh References**：扫当前关卡的 `AStaticMeshActor`，把 mesh ref 为 null 的条目写入 Message Log，带 click-to-actor token，双击在 Outliner 选中
- **Review Modified Objects**：读 `GEditor->Trans` 的 transaction buffer，列出当前 session 内所有被动过的 actor。每条可以打开 Details 看具体哪些属性、哪些 sub-object 发生了变化（从 `FTransaction::GenerateDiff` 拿的 per-property delta），也可以勾选一组 actor 整体移动到别的 sub-level

其余入口仍在开发迭代中，见下方"功能状态"。

## 与相关方案的对比

与同作者的 [`uasset-json-exporter`](https://github.com/hchia93/uasset-json-exporter) 是职责二分，不是互斥：

| 维度 | ue-map-utils（本插件） | uasset-json-exporter |
|---|---|---|
| 运行模式 | In-editor，Slate panel + 菜单 | Commandlet（editor 关闭时跑） |
| 输出 | Message Log、交互 dialog、（计划中）topic-focused JSON | 全量结构 JSON（delta-from-archetype） |
| 数据粒度 | Session / 关卡内 actor 层面 | 完整 level / BP / DataTable / ... |
| 适合任务 | LD 日常审计、session 改动回溯 | 静态分析、AI 消费整体结构、资产批量核对 |

与 UE 原生工具对比：

| 机制 | 短板 | 本插件如何覆盖 |
|---|---|---|
| Map Check (F7) | 每张图手动，不聚合，LD 不会主动开 | Message Log 可多张图叠加，click-to-actor |
| Transaction History | 能 undo 但不能"只看不动"，也没有 per-property diff UI | Review Modified 把 session 内改过的 actor 全列出，带属性级 delta |
| Reference Viewer | 解资产依赖，不解关卡内 actor 层面的 ref null | 直接扫 `TActorIterator<AStaticMeshActor>` |

## 功能状态

| 功能 | 入口 | 状态 |
|---|---|---|
| Audit StaticMesh References | Window → Map Utils → Audit 按钮 | ✅ 稳定 |
| Review Modified Objects | Window → Map Utils → Review 按钮 | ✅ 稳定 |
| Move Checked Actors to Level | Review dialog 内部的 "Move to Level" | 🧪 伴随 Review 出现，基本流程通，边缘 case 未充分测试 |
| Export StaticMesh Context | Window → Map Utils → Export 按钮 | 🧪 代码在，尚未实际投产验证 |
| Export Collision Context | 同上 | 🧪 代码在，尚未实际投产验证 |
| Replace StaticMesh | Outliner 右键 `AStaticMeshActor` → "Map Utils > Replace StaticMesh..." | 🧪 代码在，尚未实际投产验证 |
| Convert Selected to Blocking Volume | Window → Map Utils → Action 按钮 | ❌ 当前不 work，保留入口待修复 |

## 使用方法

### 打开面板

在 Level Editor 的 **Window** 菜单下找到 **Map Utils**，点击打开。面板是 `NomadTab`，可以 dock 到 Details 旁、Outliner 下方，或任意位置。

### Audit StaticMesh References

1. 打开当前关卡
2. Map Utils 面板 → 点 "Audit StaticMesh References"
3. Message Log 的 `MapUtils` 分类列出所有 null mesh 的 actor，每条带 click-to-actor token，点击即可在 Outliner 里定位

这是纯只读操作，不动关卡。

### Review Modified Objects

1. 在 editor 里正常工作：挪 actor、改属性、spawn / 删除
2. Map Utils 面板 → 点 "Review Modified Objects"
3. 弹出 dialog 列表，展示这一 session 内所有被 transaction 记录过的 actor（按 level + actor 名排序）
4. 想看某一个 actor 具体改了什么：选中条目 → 点 "Details"，弹出 per-property / per-sub-object 的 delta 列表
5. 想把其中一组 actor 批量挪到另一个 sub-level：勾选若干行 → 点 "Move to Level"，选目标 level

数据来源是 `GEditor->Trans` 里的 transaction buffer，跟着 editor session 走，**editor 重启后清空**。destroyed / GC 掉的 actor 会被自动跳过。

## 集成到你的项目

### 方式 1：作为项目插件

1. 将 `src/` 目录下的内容复制到你项目的 `Plugins/MapUtils/`
2. 在 `.uproject` 的 `Plugins` 数组中添加：

```json
{
    "Name": "MapUtils",
    "Enabled": true
}
```

插件 `EnabledByDefault: false`，所以不会自动加载，必须在宿主 `.uproject` 显式 enable。

3. 重新生成项目文件并编译（Development Editor）
4. 打开 editor，Window 菜单应出现 "Map Utils"

### 方式 2：作为引擎插件

将 `src/` 目录下的内容复制到 `<UE_PATH>/Engine/Plugins/Editor/MapUtils/`，所有项目共享。

### 前置条件

- Unreal Engine 5.7
- Editor-only 模块（不打包进 runtime）
- 目前只在 Win64 验证过（其他平台理论上可行，未测试）

## Roadmap

- 修复 Convert to Blocking Volume
- Context Export 投产验证 + 与 `uasset-json-exporter` 的分工落地
- 更多 audit topic：Lighting / Materials / Geometry overlap / Blueprint refs
- Review dialog 增加跨 session 快照 / 持久化，解决"editor 一重启就忘"的限制

## License

[MIT](LICENSE) - Hyrex Chia
