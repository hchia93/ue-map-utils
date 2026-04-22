# ue-map-utils

![Claude Code](https://img.shields.io/badge/Claude_Code-black?style=flat&logo=anthropic&logoColor=white)
![Unreal Engine 5](https://img.shields.io/badge/Unreal_Engine-5.7-blue?logo=unrealengine&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

Level Designer 工作流插件：在 UE Editor 内做关卡内资产完整性审计、碰撞清理、以及把上下文导出给 AI 工具链消费。

## 它解决什么问题

在 UE 关卡里做 whitebox / blockout / 维护的 LD 工作流中，以下场景反复出现：

- **StaticMeshActor 上的 mesh ref 被 null-out**：资产被删、merge 冲突、redirector 丢失，关卡还能打开但东西消失了，playtest / QA 前没人发现
- **薄片 StaticMesh 沿墙堆 collision**：数量多、维护成本高、容易漏缝隙；StaticMesh 本身有不必要的渲染开销，物理查询效率也不如单一 volume
- **修 broken mesh ref 需要历史上下文**：designer 通常不知道 null 位置原来引用的是什么 mesh，靠人肉查版本控制 log 不现实，需要 AI 辅助判断
- **批量替换 mesh 没有省力入口**：手工点每个 actor 的 Details → StaticMesh 字段太慢
- **UE 原生 Map Check (F7) 每张图手动**：只看当前打开的 level，结果不聚合，LD 不会主动开

这些场景的共性：问题发生在 **关卡内 Actor 层面**（不是资产本身），需要一个 Editor 内的、带交互和 AI 桥接的工具箱。

## 它如何解决

插件是一个 Editor-only UE Plugin，在 Level Editor 的 Window 菜单下暴露一个可 dock 的 Slate 面板，提供三类能力：

- **Audit**（只读检测）：扫当前关卡的 StaticMeshActor，null mesh 的位置写入 Message Log，带 click-to-actor token，双击跳到 Outliner 选中该 actor
- **Action**（确定性修改）：`Convert Selected to Blocking Volume`（空间聚类合并）、`Replace StaticMesh`（Outliner 右键批量替换），所有修改走 `FScopedTransaction`，单 Ctrl+Z 整体还原
- **Context Export**（给 AI 消费）：按话题导出 JSON（`Intermediate/MapUtilsContext/*.json`），只包含该话题闭环依赖的字段，AI 工具可直接读

插件**不做分析 / 建议 / AI 推测**：那是 AI 工具的事。插件是工具箱（primitive + 结构化输出），决策权在 designer + AI。

## 与相关方案的对比

与同作者的 [`uasset-json-exporter`](https://github.com/hchia93/uasset-json-exporter) 是职责二分，不是互斥：

| 维度 | ue-map-utils（本插件） | uasset-json-exporter |
|---|---|---|
| 运行模式 | In-editor，Slate panel + 菜单 | Commandlet（editor 关闭时跑） |
| 输出 | Message Log、topic-focused JSON、直接修改关卡 | 全量结构 JSON（delta-from-archetype） |
| 数据粒度 | 话题裁剪（StaticMesh refs / Collision candidates） | 完整 level / BP / DataTable / ... |
| 适合任务 | LD 日常交互、精准修复、快速 audit | 静态分析、AI 消费整体结构、资产批量核对 |
| 写回资产 | 是（Replace / Convert 内联） | 否（纯读） |

**选型建议**：

- LD 在 editor 里做日常维护、精确选中几个 actor 做 convert / replace → 用本插件
- 需要把整张 level 的完整结构丢给 AI 做静态分析 → 用 exporter
- 两者配合：AI 读 exporter 的整体 JSON + 本插件的话题 JSON，建议具体 action，designer 在本插件 panel 里执行 primitive

与 UE 原生工具对比：

| 机制 | 短板 | 本插件如何覆盖 |
|---|---|---|
| Map Check (F7) | 每张图手动，不聚合 | Message Log 可多张图叠加，click-to-actor |
| Reference Viewer | 解资产依赖，不解关卡内 actor 层面的 ref null | 直接扫 `TActorIterator<AStaticMeshActor>` |
| Fix Up Redirectors | 只改 redirector，不解资产已删的情况 | Replace primitive 让 designer 选替换目标 |

## 功能清单

| 功能 | 入口 | 产出 |
|---|---|---|
| **Audit StaticMesh Refs** | Window → Map Utils → Audit 按钮 | Message Log `MapUtils` 分类，null mesh 条目带 actor token |
| **Convert Selected to Blocking Volume** | Window → Map Utils → Action 按钮（先在 Outliner 选 actor） | 选中的 `AStaticMeshActor` 聚类合并为 `ABlockingVolume`，源 actor 被删；新 volume 放入首个选中 actor 所在的 level；Ctrl+Z 整体还原 |
| **Replace StaticMesh** | Outliner 右键一个或多个 `AStaticMeshActor` → "Map Utils > Replace StaticMesh..." | 弹 asset picker，选中 StaticMesh 批量替换，Ctrl+Z 还原 |
| **Export StaticMesh Context** | Window → Map Utils → Export 按钮 | `Intermediate/MapUtilsContext/staticmesh-<level>-<ts>.json`：actor name / path / level / transform / mesh path / materials / bounds |
| **Export Collision Context** | 同上 | `Intermediate/MapUtilsContext/collision-<level>-<ts>.json`：actor name / bounds / collision profile / enabled / hidden / mesh path |

## 使用方法

### 打开面板

在 Level Editor 的 **Window** 菜单下找到 **Map Utils**，点击打开。面板是 `NomadTab`，可以 dock 到 Details 旁、Outliner 下方，或任意位置。

### Convert 工作流

1. 在 Outliner 或 Viewport 选中若干个 `AStaticMeshActor`（通常是 hidden + collision enabled 的薄片 mesh，但本插件不做启发式筛选，由 designer 自己决定）
2. Map Utils 面板 → 点 "Convert Selected to Blocking Volume"
3. 选中的 actor 消失，关卡里出现若干 `ABlockingVolume`（按空间聚类合并，默认容差 10 units）
4. Message Log 会打 summary："Converted N actor(s) into M BlockingVolume(s) across K cluster(s)"
5. 不满意就 `Ctrl+Z`，整个操作作为一个 undo step 还原

聚类算法是 Union-Find 在 world-space AABB 上运行，默认容差 `10.0f` units，AABB 在该容差下有重叠就归为一组。每组一个 BlockingVolume，位置放在组合 AABB 的中心，尺寸覆盖整个组合 AABB。

新 volume 的 level：**第一个被选中的 actor 所在的 level**（支持 sub-level，`SpawnParameters.OverrideLevel = Actors[0]->GetLevel()`）。

### Replace 工作流

1. Outliner 选中一个或多个 `AStaticMeshActor`
2. 右键 → "Map Utils > Replace StaticMesh..."
3. 弹出 asset picker，只列 `UStaticMesh` 类型
4. 选中一个 → 被选 actor 的 mesh 全部换成新 mesh
5. `Ctrl+Z` 还原

### Context Export 工作流

1. 打开当前关卡
2. Map Utils 面板 → Export 按钮（StaticMesh 或 Collision）
3. JSON 写到 `Intermediate/MapUtilsContext/<topic>-<level>-<timestamp>.json`
4. Message Log 显示 `"Exported N actor(s) to <path>"`
5. 把 JSON 内容贴给 AI 工具（或让它直接读文件），请它分析

### 输出示例

<details>
<summary>StaticMesh Refs</summary>

```json
{
    "ExporterVersion": "MapUtils/1.0",
    "Topic": "StaticMeshRefs",
    "Level": "L_Map",
    "Timestamp": "2026-04-22T15:42:08Z",
    "Actors": [
        {
            "Name": "StaticMeshActor_1",
            "Path": "/Game/Maps/L_Map.L_Map:PersistentLevel.StaticMeshActor_1",
            "Level": "/Game/Maps/L_Map",
            "Location": { "X": 1200.0, "Y": -450.0, "Z": 100.0 },
            "Rotation": { "Pitch": 0, "Yaw": 90, "Roll": 0 },
            "Scale": { "X": 1.0, "Y": 1.0, "Z": 1.0 },
            "MeshPath": "",
            "Materials": []
        }
    ]
}
```
空 `MeshPath` 代表该 actor 的 StaticMesh ref 是 null，这是 audit 要关注的条目。
</details>

<details>
<summary>Collision Candidates</summary>

```json
{
    "ExporterVersion": "MapUtils/1.0",
    "Topic": "CollisionCandidates",
    "Level": "L_Map",
    "Timestamp": "2026-04-22T15:45:12Z",
    "Actors": [
        {
            "Name": "StaticMeshActor_1",
            "Path": "/Game/Maps/L_Map.L_Map:PersistentLevel.StaticMeshActor_1",
            "Level": "/Game/Maps/L_Map",
            "Location": { "X": 500.0, "Y": 0.0, "Z": 50.0 },
            "bHidden": true,
            "CollisionProfile": "BlockAll",
            "CollisionEnabled": "QueryAndPhysics",
            "MeshPath": "/Engine/BasicShapes/Plane.Plane",
            "WorldBounds": {
                "Min": { "X": 500.0, "Y": -100.0, "Z": 0.0 },
                "Max": { "X": 500.0, "Y": 100.0, "Z": 100.0 }
            }
        }
    ]
}
```
只收录 `CollisionEnabled != NoCollision` 的 actor。`bHidden = true` 通常暗示 "collision-only" 用途的薄片，是 Convert to BlockingVolume 的典型目标。
</details>

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

## 配合 Claude Code 使用

如果你用 [Claude Code](https://claude.ai/code) 作为 AI 协作工具，可以在宿主项目 `.claude/CLAUDE.md` 加以下段落让 AI 知道这个工具：

```markdown
## Tooling: Map Utils

Plugin: `Plugins/MapUtils` (Editor-only)

### Entry points

- Window → Map Utils → opens dockable Slate panel
- Outliner right-click on AStaticMeshActor → "Map Utils > Replace StaticMesh..."

### Action primitives

- Audit StaticMesh Refs (current level) → Message Log with click-to-actor tokens
- Convert Selected to Blocking Volume → Union-Find cluster + UCubeBuilder spawn, source actors destroyed; single Ctrl+Z reverts
- Replace StaticMesh → asset picker, batch apply to selected AStaticMeshActors

### Context export (for AI consumption)

- `Export StaticMesh Context` → Intermediate/MapUtilsContext/staticmesh-<level>-<ts>.json
- `Export Collision Context` → Intermediate/MapUtilsContext/collision-<level>-<ts>.json

JSON is topic-focused (not a full-level dump). Use these when AI needs to reason about actor-level refs or collision candidates in the current level.

### When to use

- LD reports "level opens but stuff is missing" → Audit StaticMesh Refs
- Whitebox/blockout map has too many thin collision meshes → Export Collision Context + let AI suggest, then Convert Selected to BlockingVolume
- A mesh asset was renamed/moved and N actors have null refs → Export StaticMesh Context + AI suggests replacement, designer uses Replace StaticMesh primitive

### Policy

- The plugin executes primitives; it does NOT analyze, suggest, or read version-control history
- Analysis is AI's job (consume the JSON and make suggestions)
- Designer is the final executor (pick a suggested primitive)
```

AI 会在相关任务中自动调用本插件的 primitive 和 export。

## 与 uasset-json-exporter 配合

两个插件可以并存：

- **exporter** 产出关卡全量 JSON（在 `Intermediate/UAssetExport/Game/Maps/*.json`），适合让 AI 做结构化分析
- **map-utils** 产出话题 JSON + 执行 primitive，适合 designer 在 editor 内交互 + AI 分析后精准修复

典型链路：designer 打开关卡 → map-utils 跑 audit 看 null refs → export staticmesh context → 把 JSON 给 AI → AI 结合版本控制 log 判断原 mesh 是什么 → 建议替换目标 → designer 在 Outliner 里 Ctrl+选中那些 null actor → map-utils 的 Replace StaticMesh 批量替换。

## Roadmap

- Slate panel 里增加 audit result 的列表视图（不仅仅在 Message Log）
- 更多 audit topic：Lighting / Materials / Geometry overlap / Blueprint refs
- Batch Replace UI（一次替换分散在多个 level 的同 mesh 引用）
- `ISourceControlModule` commit-time 自动审计

## License

[MIT](LICENSE) - Hyrex Chia
