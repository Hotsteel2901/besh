# besh — landing page (web)

面向 besh（一个用 C 写成的 bash 兼容 shell）的赛博朋克风格单页落地页，
基于 **Next.js 14 + React 18 + Tailwind CSS** 构建，静态导出部署到 GitHub Pages。

## 页面内容

- **Hero** — ASCII logo、标签语、技术徽章（bash / fish / zsh / autosuggest…）
- **交互终端** — 可输入命令的演示终端，带 **fish 风格语法高亮** 与
  **自动建议**（输入前缀后按 `→` 接受），演示 `abbr`、`{1..5}`、`setopt`、
  `pushd` / `dirs` 等新特性
- **特性区** — `/core`（bash 兼容核心）与 `/fish + zsh`（现代特性）两组卡片
- **内建命令网格** — 38 个内建命令（含 abbr / pushd / dirs / setopt…）
- **迷你游戏** — PacEat、SpeedType、AsciiRain、TowerDefense
- **3D AST 可视化** — React Three Fiber 渲染的抽象语法树
- **基准面板** — 真实代码行数 / 内建命令数 / 二进制体积等统计

## 本地开发

```bash
npm install
npm run dev        # http://localhost:3000
```

## 构建与部署（GitHub Pages）

```bash
npm run build      # 静态导出到 ./out
```

`next.config.mjs` 已配置 `output: "export"` 与 `basePath: "/besh"`，
把 `out/` 目录推到 `gh-pages` 分支即可：

```bash
npm run build && npx gh-pages -d out
```

## 目录结构

```
app/
  page.tsx            # 页面组装
  layout.tsx          # 元数据 / 全局样式
  globals.css         # Tailwind + 霓虹/CRT 主题
  components/         # Hero, Terminal, FeatureSection, BuiltinsGrid,
                      # AstVisualizer(3D), BenchmarkBar, Footer, …
  hooks/              # useTerminal, useIsDesktop, useReducedMotion
  games/              # PacEat, SpeedType, AsciiRain, TowerDefense
```
