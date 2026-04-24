# ue-map-utils

**English** | [中文](README_CN.md)

![Claude Code](https://img.shields.io/badge/Claude_Code-black?style=flat&logo=anthropic&logoColor=white)
![Unreal Engine 5](https://img.shields.io/badge/Unreal_Engine-5.7-blue?logo=unrealengine&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

A Level Designer workflow plugin: in-editor actor-level integrity audit, session-level change review, plus context export for AI toolchains.

## What problem it solves

In UE whitebox / blockout / maintenance workflows for level designers, the following scenarios keep recurring:

- **StaticMeshActor mesh refs get null-ed out**: the asset was deleted, a merge went wrong, a redirector was lost. The level still opens but stuff is missing, and nobody notices before playtest / QA
- **"I don't remember what I touched this round"**: a single editor session moves dozens of actors and tweaks properties. Before Ctrl+S you want to review what changed and how, but UE does not surface this directly
- **Native Map Check (F7) is per-level manual**: only looks at the currently open level, does not aggregate results, LDs rarely open it on their own

The common thread: the problem lives at the **in-level actor layer** (not the asset itself), and needs an editor-resident, interactive tool panel.

## How it solves it

The plugin is an Editor-only UE plugin that exposes a dockable Slate panel under the Level Editor's Window menu.

Two features are currently stable:

- **Audit StaticMesh References**: scans the current level for `AStaticMeshActor` whose mesh ref is null, writes each to Message Log with a click-to-actor token (double-click selects in the Outliner)
- **Review Modified Objects**: reads the `GEditor->Trans` transaction buffer and lists every actor touched during the current session. Each entry has a Details button that shows which properties and which sub-objects changed (per-property delta from `FTransaction::GenerateDiff`). You can also tick a subset of rows and move them as a group to another sub-level

Other entries are still being iterated on, see "Feature status" below.

## Comparison with related approaches

With the same author's [`uasset-json-exporter`](https://github.com/hchia93/uasset-json-exporter), the split is by responsibility, not exclusivity:

| Dimension | ue-map-utils (this plugin) | uasset-json-exporter |
|---|---|---|
| Run mode | In-editor, Slate panel + menu | Commandlet (editor closed) |
| Output | Message Log, interactive dialogs, (planned) topic-focused JSON | Full structural JSON (delta-from-archetype) |
| Data grain | Session / in-level actor layer | Entire level / BP / DataTable / ... |
| Suitable tasks | LD day-to-day audit, session change review | Static analysis, AI-consumed full structure, asset-batch audit |

Compared with UE native tooling:

| Mechanism | Shortcoming | How this plugin covers it |
|---|---|---|
| Map Check (F7) | Per-level manual, does not aggregate, LDs rarely open it | Message Log can accumulate across levels, click-to-actor |
| Transaction History | Can undo but cannot "just view", no per-property diff UI | Review Modified lists every session-touched actor, with property-level delta |
| Reference Viewer | Resolves asset dependencies, not actor-level ref nulls | Direct `TActorIterator<AStaticMeshActor>` scan |

## Feature status

| Feature | Entry | Status |
|---|---|---|
| Audit StaticMesh References | Window → Map Utils → Audit button | ✅ Stable |
| Review Modified Objects | Window → Map Utils → Review button | ✅ Stable |
| Move Checked Actors to Level | "Move to Level" inside the Review dialog | 🧪 Ships with Review, happy path works, edge cases not fully covered |
| Export StaticMesh Context | Window → Map Utils → Export button | 🧪 Code present, not yet exercised in production |
| Export Collision Context | Same as above | 🧪 Code present, not yet exercised in production |
| Replace StaticMesh | Outliner right-click on `AStaticMeshActor` → "Map Utils > Replace StaticMesh..." | 🧪 Code present, not yet exercised in production |
| Convert Selected to Blocking Volume | Window → Map Utils → Action button | ❌ Currently not working, entry preserved pending fix |

## Usage

### Open the panel

Under the Level Editor's **Window** menu, find **Map Utils** and click to open. The panel is a `NomadTab`, dockable next to Details, under Outliner, or anywhere you like.

### Audit StaticMesh References

1. Open the target level
2. Map Utils panel → click "Audit StaticMesh References"
3. The `MapUtils` category in Message Log lists every null-mesh actor, each with a click-to-actor token that locates it in the Outliner

Read-only. Does not touch the level.

### Review Modified Objects

1. Work normally in the editor: move actors, tweak properties, spawn / delete
2. Map Utils panel → click "Review Modified Objects"
3. A dialog lists every actor recorded in this session's transaction buffer, sorted by level then actor name
4. To see what changed on one actor: select the row → click "Details" to pop up a per-property / per-sub-object delta list
5. To bulk-move a group to another sub-level: tick the rows → click "Move to Level" and pick the destination

The data source is the `GEditor->Trans` transaction buffer, which follows the editor session, **cleared on editor restart**. Destroyed / GC'd actors are skipped automatically.

## Integrating into your project

### Option 1: as a project plugin

1. Copy the contents of `src/` into your project's `Plugins/MapUtils/`
2. Add to the `Plugins` array in `.uproject`:

```json
{
    "Name": "MapUtils",
    "Enabled": true
}
```

The plugin sets `EnabledByDefault: false`, so it will not auto-load. The host `.uproject` must enable it explicitly.

3. Regenerate project files and build (Development Editor)
4. Open the editor, "Map Utils" should appear in the Window menu

### Option 2: as an engine plugin

Copy the contents of `src/` into `<UE_PATH>/Engine/Plugins/Editor/MapUtils/`, shared by all projects.

### Prerequisites

- Unreal Engine 5.7
- Editor-only module (not packaged into runtime)
- Only verified on Win64 so far (other platforms should work in theory, untested)

## Roadmap

- Fix Convert to Blocking Volume
- Production-validate Context Export and settle the division of labor with `uasset-json-exporter`
- More audit topics: Lighting / Materials / Geometry overlap / Blueprint refs
- Add cross-session snapshot / persistence to the Review dialog, to remove the "editor restart wipes it" limitation

## License

[MIT](LICENSE) - Hyrex Chia
