# besh Cyber Terminal Landing Page — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a maximalist cyberpunk terminal-themed landing page for the besh shell project with interactive mini-games, 3D effects, and progressive enhancement for mobile.

**Architecture:** Single-page Next.js 14 App Router app. All sections live in `app/page.tsx` composing components from `app/components/` and `app/games/`. Desktop (>=1024px) gets full 3D + GSAP animations; mobile falls back to simplified CSS/SVG versions via `useIsDesktop()` and `useReducedMotion()` hooks. Three.js particles for matrix rain background; Canvas API for mini-games; GSAP for scroll-triggered animations.

**Tech Stack:** Next.js 14, TypeScript, Tailwind CSS, GSAP, Three.js/React Three Fiber, Canvas API

## Global Constraints
- Node >= 18
- No external APIs or databases
- All assets inline (no external images)
- Single page, no routing
- Desktop >=1024px gets full features; mobile <1024px gets simplified
- All touch targets >= 44px
- Body text minimum 16px on mobile
- `prefers-reduced-motion` respected globally

---

### Task 1: Scaffold Next.js Project

**Files:**
- Create: `/root/001/web/` directory and all Next.js scaffold files
- Modify: `tailwind.config.ts`, `globals.css`

**Interfaces:**
- Produces: Working Next.js dev server with Tailwind and TypeScript

- [ ] **Step 1: Create Next.js project**

```bash
cd /root/001 && npx create-next-app@14 web --typescript --tailwind --eslint --app --src-dir=false --import-alias="@/*" --no-git 2>&1 | tail -5
```

- [ ] **Step 2: Install additional dependencies**

```bash
cd /root/001/web && npm install gsap three @react-three/fiber @react-three/drei @types/three 2>&1 | tail -5
```

- [ ] **Step 3: Verify dev server starts**

```bash
cd /root/001/web && timeout 10 npm run dev 2>&1 || true
```
Expected: "Ready" or "started server" message with localhost URL.

- [ ] **Step 4: Clean default boilerplate**

Remove default content from `app/page.tsx`, replace with minimal placeholder:

```tsx
export default function Home() {
  return <main className="min-h-screen bg-[#0F0F23] text-[#E2E8F0]"></main>;
}
```

Clean `app/layout.tsx` to minimal:

```tsx
import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "besh — Cyber Shell",
  description: "A bash-compatible shell written in C",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className="dark">
      <body className="bg-[#0F0F23] text-[#E2E8F0]">{children}</body>
    </html>
  );
}
```

- [ ] **Step 5: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: scaffold Next.js cyber terminal project"
```

---

### Task 2: Theme + Global Styles + Fonts

**Files:**
- Modify: `app/globals.css`
- Modify: `tailwind.config.ts`
- Modify: `app/layout.tsx` (add fonts)

**Interfaces:**
- Produces: CSS custom properties for all design tokens; Orbitron + JetBrains Mono fonts loaded; Tailwind theme extensions

- [ ] **Step 1: Write Tailwind config with cyberpunk theme**

Replace `tailwind.config.ts`:

```ts
import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./app/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        "neon-purple": "#7C3AED",
        "accent-rose": "#F43F5E",
        "void-bg": "#0F0F23",
        "terminal-green": "#00FF41",
        "dark-panel": "#1A1030",
        "muted-border": "#4C1D95",
        violet: "#A78BFA",
        foreground: "#E2E8F0",
      },
      fontFamily: {
        display: ["Orbitron", "sans-serif"],
        mono: ["JetBrains Mono", "monospace"],
      },
      animation: {
        "crt-flicker": "crt-flicker 0.15s infinite alternate",
        "neon-pulse": "neon-pulse 2s ease-in-out infinite",
        "glitch": "glitch 0.3s ease-in-out",
        "scanline": "scanline 8s linear infinite",
        "typewriter": "typewriter 2s steps(20) forwards",
        "float": "float 6s ease-in-out infinite",
        "matrix-fall": "matrix-fall 1.5s linear infinite",
      },
      keyframes: {
        "crt-flicker": {
          "0%, 100%": { opacity: "1" },
          "50%": { opacity: "0.97" },
        },
        "neon-pulse": {
          "0%, 100%": { boxShadow: "0 0 5px #7C3AED, 0 0 20px #7C3AED" },
          "50%": { boxShadow: "0 0 15px #7C3AED, 0 0 40px #7C3AED, 0 0 60px #7C3AED" },
        },
        "glitch": {
          "0%, 100%": { transform: "translateX(0)" },
          "20%": { transform: "translateX(-2px)" },
          "40%": { transform: "translateX(2px)" },
          "60%": { transform: "translateX(-1px)" },
          "80%": { transform: "translateX(1px)" },
        },
        "scanline": {
          "0%": { transform: "translateY(0)" },
          "100%": { transform: "translateY(100vh)" },
        },
        "typewriter": {
          "0%": { width: "0" },
          "100%": { width: "100%" },
        },
        "float": {
          "0%, 100%": { transform: "translateY(0)" },
          "50%": { transform: "translateY(-10px)" },
        },
        "matrix-fall": {
          "0%": { transform: "translateY(-100%)", opacity: "1" },
          "100%": { transform: "translateY(100vh)", opacity: "0" },
        },
      },
    },
  },
  plugins: [],
};
export default config;
```

- [ ] **Step 2: Write globals.css with CRT overlay**

Replace `app/globals.css`:

```css
@tailwind base;
@tailwind components;
@tailwind utilities;

@import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500&family=Orbitron:wght@700;900&display=swap');

:root {
  --neon-purple: #7C3AED;
  --accent-rose: #F43F5E;
  --void-bg: #0F0F23;
  --terminal-green: #00FF41;
  --dark-panel: #1A1030;
}

* {
  scrollbar-width: thin;
  scrollbar-color: var(--neon-purple) var(--void-bg);
}

body {
  font-family: 'JetBrains Mono', monospace;
  background: var(--void-bg);
  color: #E2E8F0;
  overflow-x: hidden;
}

.crt-overlay::before {
  content: "";
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: repeating-linear-gradient(
    0deg,
    rgba(0, 0, 0, 0.08) 0px,
    rgba(0, 0, 0, 0.08) 2px,
    transparent 2px,
    transparent 4px
  );
  pointer-events: none;
  z-index: 9999;
  animation: scanline 8s linear infinite;
}

.neon-glow {
  text-shadow: 0 0 5px var(--neon-purple), 0 0 20px var(--neon-purple), 0 0 40px var(--neon-purple);
}

.neon-glow-green {
  text-shadow: 0 0 5px var(--terminal-green), 0 0 20px var(--terminal-green);
}

.neon-border {
  border: 1px solid var(--neon-purple);
  box-shadow: 0 0 5px var(--neon-purple), 0 0 20px var(--neon-purple), inset 0 0 5px rgba(124, 58, 237, 0.2);
}

@layer utilities {
  .text-glow {
    text-shadow: 0 0 7px var(--neon-purple), 0 0 20px var(--neon-purple);
  }
}

@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after {
    animation-duration: 0.01ms !important;
    animation-iteration-count: 1 !important;
    transition-duration: 0.01ms !important;
  }
}
```

- [ ] **Step 3: Verify fonts load and styles apply**

```bash
cd /root/001/web && timeout 10 npm run dev 2>&1 || true
```
Open http://localhost:3000 and check that dark background renders.

- [ ] **Step 4: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add cyberpunk theme, fonts, and CRT overlay styles"
```

---

### Task 3: Core Hooks

**Files:**
- Create: `app/hooks/useIsDesktop.ts`
- Create: `app/hooks/useReducedMotion.ts`
- Create: `app/hooks/useTerminal.ts`

**Interfaces:**
- Consumes: Nothing (first hooks)
- Produces: `useIsDesktop(): boolean` — returns true when window width >= 1024
- Produces: `useReducedMotion(): boolean` — returns true when user prefers reduced motion
- Produces: `useTerminal()` — returns `{ history: string[], pushLine(line: string): void, clear(): void }`

- [ ] **Step 1: Create useIsDesktop hook**

Write `app/hooks/useIsDesktop.ts`:

```tsx
"use client";
import { useState, useEffect } from "react";

export function useIsDesktop(): boolean {
  const [isDesktop, setIsDesktop] = useState(false);

  useEffect(() => {
    const check = () => setIsDesktop(window.innerWidth >= 1024);
    check();
    window.addEventListener("resize", check);
    return () => window.removeEventListener("resize", check);
  }, []);

  return isDesktop;
}
```

- [ ] **Step 2: Create useReducedMotion hook**

Write `app/hooks/useReducedMotion.ts`:

```tsx
"use client";
import { useState, useEffect } from "react";

export function useReducedMotion(): boolean {
  const [reduced, setReduced] = useState(false);

  useEffect(() => {
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    setReduced(mq.matches);
    const handler = (e: MediaQueryListEvent) => setReduced(e.matches);
    mq.addEventListener("change", handler);
    return () => mq.removeEventListener("change", handler);
  }, []);

  return reduced;
}
```

- [ ] **Step 3: Create useTerminal hook**

Write `app/hooks/useTerminal.ts`:

```tsx
"use client";
import { useState, useCallback } from "react";

const COMMANDS: Record<string, string[]> = {
  help: ["besh commands:", "  echo cd pwd ls cat export unset alias source exit", "  jobs fg bg history set read test true false exec", "  shift times trap umask break continue return help"],
  whoami: ["root@cybershell"],
  pwd: ["/home/user"],
  ls: ["Makefile  builtins.c  executor.c  expand.c  lexer.c  main.c  parser.c  shell.h  besh"],
  "cat shell.h": ["/* besh - a bash-compatible shell written in C */", "#define SHELL_VERSION \"1.0.0\"", "typedef struct { int argc; char **argv; } Command;"],
  "echo hello": ["hello"],
  "echo $HOME": ["/root"],
  "echo $((1+1))": ["2"],
  date: [new Date().toString()],
  uname: ["Linux cybershell 6.1.0 x86_64"],
  uptime: ["up 42 days, 7 hours, 13 minutes"],
  neofetch: [
    "         -/oyddmdhs+:.                root@cybershell",
    "     -odNMMMMMMMMMNmdy+-              OS: besh 1.0.0",
    "   -yNMMMMMMMMMMMMMMMNm/             Shell: besh (cyber edition)",
    "  :mMMMMMMMMMMMMMMMNmdm+-            Terminal: /dev/tty1",
    "  .+shdmmNNNNNNNdyo/-                CPU: CyberCore i9-1337K",
    "  .-:////////::-.                    Memory: 64GB DDR6",
  ],
};

export function useTerminal() {
  const [history, setHistory] = useState<string[]>(["besh v1.0.0 — Type 'help' for commands", ""]);

  const pushLine = useCallback((input: string) => {
    setHistory((prev) => {
      const next = [...prev, `$ ${input}`];
      const cmd = input.trim().toLowerCase();
      const response = COMMANDS[cmd] || (cmd ? [`besh: command not found: ${input.trim()}`] : []);
      return [...next, ...response, ""];
    });
  }, []);

  const clear = useCallback(() => {
    setHistory(["besh v1.0.0 — Type 'help' for commands", ""]);
  }, []);

  return { history, pushLine, clear };
}
```

- [ ] **Step 4: Verify hooks compile**

```bash
cd /root/001/web && npx tsc --noEmit app/hooks/*.ts 2>&1
```
Expected: No errors.

- [ ] **Step 5: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add useIsDesktop, useReducedMotion, useTerminal hooks"
```

---

### Task 4: HUD Overlay (Global CRT + Scanlines + Corner Brackets)

**Files:**
- Create: `app/components/HUDOverlay.tsx`

**Interfaces:**
- Consumes: Nothing (self-contained visual layer)
- Produces: `<HUDOverlay />` — renders fixed-position CRT scanlines, corner bracket decorations, and subtle flicker overlay

- [ ] **Step 1: Write HUDOverlay**

Write `app/components/HUDOverlay.tsx`:

```tsx
"use client";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

export default function HUDOverlay() {
  const reduced = useReducedMotion();

  return (
    <div className="crt-overlay pointer-events-none fixed inset-0 z-[9999]" aria-hidden="true">
      {/* Corner brackets */}
      <div className="absolute top-4 left-4 w-8 h-8 border-t-2 border-l-2 border-[#7C3AED]/60" />
      <div className="absolute top-4 right-4 w-8 h-8 border-t-2 border-r-2 border-[#7C3AED]/60" />
      <div className="absolute bottom-4 left-4 w-8 h-8 border-b-2 border-l-2 border-[#7C3AED]/60" />
      <div className="absolute bottom-4 right-4 w-8 h-8 border-b-2 border-r-2 border-[#7C3AED]/60" />

      {/* Scan lines */}
      {!reduced && (
        <div
          className="absolute inset-0 opacity-[0.03]"
          style={{
            backgroundImage: "repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(0,255,65,0.1) 2px, rgba(0,255,65,0.1) 4px)",
            animation: "scanline 8s linear infinite",
          }}
        />
      )}

      {/* CRT vignette */}
      <div
        className="absolute inset-0"
        style={{
          background: "radial-gradient(ellipse at center, transparent 60%, rgba(15,15,35,0.8) 100%)",
        }}
      />

      {/* Subtle noise flicker */}
      {!reduced && (
        <div
          className="absolute inset-0 opacity-[0.015] animate-crt-flicker"
          style={{ background: "rgba(124,58,237,0.3)" }}
        />
      )}
    </div>
  );
}
```

- [ ] **Step 2: Add HUDOverlay to layout**

Modify `app/layout.tsx`:

```tsx
import type { Metadata } from "next";
import HUDOverlay from "./components/HUDOverlay";
import "./globals.css";

export const metadata: Metadata = {
  title: "besh — Cyber Shell",
  description: "A bash-compatible shell written in C",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className="dark">
      <body className="bg-[#0F0F23] text-[#E2E8F0] font-mono">
        {children}
        <HUDOverlay />
      </body>
    </html>
  );
}
```

- [ ] **Step 3: Verify layout renders**

```bash
cd /root/001/web && timeout 10 npm run dev 2>&1 || true
```
Visually verify corner brackets and CRT vignette appear on page.

- [ ] **Step 4: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add HUD overlay with CRT scanlines and corner brackets"
```

---

### Task 5: Matrix Rain Background

**Files:**
- Create: `app/components/MatrixRain.tsx`

**Interfaces:**
- Consumes: `useIsDesktop()`, `useReducedMotion()`
- Produces: `<MatrixRain />` — renders 3D particle rain (desktop) or CSS character rain (mobile)

- [ ] **Step 1: Write MatrixRainCanvas (3D version for desktop)**

Write `app/components/MatrixRain.tsx`:

```tsx
"use client";
import { useRef, useMemo } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

const CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$#@%&*()_+-=[]{}|;:<>?,./";

function RainDrop({ index, total }: { index: number; total: number }) {
  const meshRef = useRef<THREE.Mesh>(null);
  const col = useMemo(() => (index / total) * 30 - 15, [index, total]);
  const speed = useMemo(() => 0.02 + Math.random() * 0.08, []);
  const chars = useMemo(() => {
    const len = 5 + Math.floor(Math.random() * 10);
    return Array.from({ length: len }, () => CHARS[Math.floor(Math.random() * CHARS.length)]);
  }, []);

  useFrame(() => {
    if (meshRef.current) {
      meshRef.current.position.y -= speed;
      if (meshRef.current.position.y < -10) {
        meshRef.current.position.y = 10;
      }
    }
  });

  return (
    <mesh ref={meshRef} position={[col, Math.random() * 20 - 10, -5]}>
      <planeGeometry args={[0.5, chars.length * 0.3]} />
      <meshBasicMaterial color="#00FF41" transparent opacity={0.6} />
    </mesh>
  );
}

function RainScene() {
  const drops = useMemo(() => Array.from({ length: 80 }, (_, i) => i), []);

  return (
    <>
      <ambientLight intensity={0} />
      {drops.map((i) => (
        <RainDrop key={i} index={i} total={drops.length} />
      ))}
    </>
  );
}

function CSSRain() {
  return (
    <div className="absolute inset-0 overflow-hidden pointer-events-none opacity-20">
      {Array.from({ length: 40 }).map((_, i) => (
        <span
          key={i}
          className="absolute text-[#00FF41] text-xs font-mono animate-matrix-fall"
          style={{
            left: `${(i / 40) * 100}%`,
            animationDelay: `${Math.random() * 3}s`,
            animationDuration: `${1 + Math.random() * 2}s`,
          }}
        >
          {CHARS[Math.floor(Math.random() * CHARS.length)]}
        </span>
      ))}
    </div>
  );
}

export default function MatrixRain() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();

  if (reduced) return null;

  if (isDesktop) {
    return (
      <div className="absolute inset-0 z-0">
        <Canvas camera={{ position: [0, 0, 10], fov: 60 }} dpr={[1, 1.5]}>
          <RainScene />
        </Canvas>
      </div>
    );
  }

  return <CSSRain />;
}
```

- [ ] **Step 2: Verify TypeScript compilation**

```bash
cd /root/001/web && npx tsc --noEmit 2>&1 | head -20
```
Expected: No errors or only unrelated warnings.

- [ ] **Step 3: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add MatrixRain 3D/CSS background component"
```

---

### Task 6: GlitchCard Component

**Files:**
- Create: `app/components/GlitchCard.tsx`

**Interfaces:**
- Consumes: Nothing
- Produces: `<GlitchCard title={string} subtitle={string} glowColor={string} />` — neon-bordered card with hover glitch animation

- [ ] **Step 1: Write GlitchCard**

Write `app/components/GlitchCard.tsx`:

```tsx
"use client";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface GlitchCardProps {
  title: string;
  subtitle?: string;
  glowColor?: string;
  children?: React.ReactNode;
}

export default function GlitchCard({ title, subtitle, glowColor = "#7C3AED", children }: GlitchCardProps) {
  const reduced = useReducedMotion();

  return (
    <div
      className="relative group p-4 rounded-sm border transition-all duration-300 cursor-default"
      style={{
        borderColor: glowColor,
        background: "linear-gradient(135deg, rgba(26,16,48,0.9), rgba(15,15,35,0.95))",
        boxShadow: `0 0 10px ${glowColor}22, 0 0 30px ${glowColor}11`,
      }}
      onMouseEnter={(e) => {
        if (reduced) return;
        (e.currentTarget as HTMLElement).style.boxShadow = `0 0 20px ${glowColor}44, 0 0 60px ${glowColor}22`;
        (e.currentTarget as HTMLElement).style.transform = "translateY(-2px) scale(1.02)";
      }}
      onMouseLeave={(e) => {
        (e.currentTarget as HTMLElement).style.boxShadow = `0 0 10px ${glowColor}22, 0 0 30px ${glowColor}11`;
        (e.currentTarget as HTMLElement).style.transform = "translateY(0) scale(1)";
      }}
    >
      {/* Glitch stripes on hover */}
      {!reduced && (
        <div className="absolute inset-0 opacity-0 group-hover:opacity-100 transition-opacity duration-75 overflow-hidden pointer-events-none rounded-sm">
          <div className="absolute inset-0 bg-[#F43F5E]/10 animate-glitch" />
          <div className="absolute inset-0 bg-[#00FF41]/10 animate-glitch" style={{ animationDelay: "0.05s" }} />
        </div>
      )}

      <h3
        className="font-display text-sm tracking-widest uppercase mb-1"
        style={{ color: glowColor, textShadow: `0 0 5px ${glowColor}` }}
      >
        {title}
      </h3>
      {subtitle && (
        <p className="text-xs text-[#E2E8F0]/70 font-mono">{subtitle}</p>
      )}
      {children}
    </div>
  );
}
```

- [ ] **Step 2: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add GlitchCard component with hover effects"
```

---

### Task 7: Terminal Component

**Files:**
- Create: `app/components/Terminal.tsx`

**Interfaces:**
- Consumes: `useTerminal()`
- Produces: `<Terminal />` — interactive terminal emulator with command input and pre-programmed responses

- [ ] **Step 1: Write Terminal component**

Write `app/components/Terminal.tsx`:

```tsx
"use client";
import { useState, useRef, useEffect, useCallback } from "react";
import { useTerminal } from "@/app/hooks/useTerminal";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";

export default function Terminal() {
  const { history, pushLine, clear } = useTerminal();
  const [input, setInput] = useState("");
  const inputRef = useRef<HTMLInputElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const isDesktop = useIsDesktop();

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [history]);

  const handleSubmit = useCallback(
    (e: React.FormEvent) => {
      e.preventDefault();
      if (input.trim() === "clear") {
        clear();
      } else {
        pushLine(input);
      }
      setInput("");
    },
    [input, pushLine, clear]
  );

  const quickCommands = ["help", "neofetch", "ls", "whoami", "uname", "echo hello"];

  return (
    <div className="w-full max-w-3xl mx-auto">
      <div className="neon-border rounded-sm overflow-hidden">
        {/* Title bar */}
        <div className="flex items-center justify-between px-4 py-2 bg-[#1A1030] border-b border-[#4C1D95]">
          <span className="text-xs text-[#00FF41] font-mono tracking-wider">
            ┌─ besh@cybershell ───────
          </span>
          <div className="flex gap-2">
            <span className="w-3 h-3 rounded-full bg-[#F43F5E]" />
            <span className="w-3 h-3 rounded-full bg-[#FBBF24]" />
            <span className="w-3 h-3 rounded-full bg-[#00FF41]" />
          </div>
        </div>

        {/* Terminal output */}
        <div
          ref={scrollRef}
          className="h-64 md:h-80 overflow-y-auto px-4 py-3 font-mono text-sm leading-relaxed"
          style={{ background: "rgba(15,15,35,0.95)" }}
        >
          {history.map((line, i) => (
            <div
              key={i}
              className={
                line.startsWith("$ ")
                  ? "text-[#7C3AED]"
                  : line === ""
                    ? "h-2"
                    : "text-[#00FF41]/80"
              }
            >
              {line || "\u00A0"}
            </div>
          ))}
        </div>

        {/* Input area */}
        <form
          onSubmit={handleSubmit}
          className="flex items-center px-4 py-2 bg-[#0F0F23] border-t border-[#4C1D95]"
        >
          <span className="text-[#7C3AED] font-mono text-sm mr-2">$</span>
          <input
            ref={inputRef}
            type="text"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            className="flex-1 bg-transparent border-none outline-none text-[#00FF41] font-mono text-sm placeholder-[#4C1D95]"
            placeholder="type a command..."
            autoFocus
            spellCheck={false}
          />
        </form>

        {/* Quick command buttons (mobile only) */}
        {!isDesktop && (
          <div className="flex flex-wrap gap-1 px-4 py-2 bg-[#0F0F23] border-t border-[#4C1D95]">
            {quickCommands.map((cmd) => (
              <button
                key={cmd}
                type="button"
                onClick={() => pushLine(cmd)}
                className="px-2 py-1 text-xs font-mono text-[#A78BFA] border border-[#4C1D95] rounded hover:bg-[#4C1D95]/20 transition-colors"
              >
                {cmd}
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Verify compilation**

```bash
cd /root/001/web && npx tsc --noEmit 2>&1 | head -20
```

- [ ] **Step 3: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add interactive Terminal component"
```

---

### Task 8: Hero Section

**Files:**
- Create: `app/components/Hero.tsx`

**Interfaces:**
- Consumes: `MatrixRain`, `useReducedMotion`
- Produces: `<Hero />` — full-viewport hero with besh ASCII logo, typewriter tagline, and CTA scroll hint

- [ ] **Step 1: Write Hero component**

Write `app/components/Hero.tsx`:

```tsx
"use client";
import { useEffect, useRef } from "react";
import { gsap } from "gsap";
import MatrixRain from "./MatrixRain";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

const BESH_ASCII = [
  "  ╔╗             ╔╗     ",
  "  ║║             ║║     ",
  "  ║╚══╦══╦═╦══╦═╝╠══╦╗ ",
  "  ║╔╗╔╣╔╗║╔╣╔╗║╔╗║║═╣║ ",
  "  ║║║╚╣╔╗║║║╚╝║╚╝║║═╣╚╗",
  "  ╚╝╚═╩╝╚╩╝╚══╩══╩══╩═╝",
];

const TAGLINE = "a bash-compatible shell written in C";

export default function Hero() {
  const taglineRef = useRef<HTMLParagraphElement>(null);
  const ctaRef = useRef<HTMLDivElement>(null);
  const reduced = useReducedMotion();

  useEffect(() => {
    if (reduced) return;
    const ctx = gsap.context(() => {
      gsap.from(taglineRef.current, { opacity: 0, y: 20, delay: 1.5, duration: 0.8 });
      gsap.from(ctaRef.current, { opacity: 0, y: 10, delay: 2.5, duration: 0.6 });
    });
    return () => ctx.revert();
  }, [reduced]);

  return (
    <section className="relative min-h-screen flex flex-col items-center justify-center overflow-hidden">
      <MatrixRain />

      <div className="relative z-10 text-center px-4">
        {/* ASCII logo */}
        <pre
          className="font-mono text-[10px] sm:text-xs md:text-sm leading-tight neon-glow mb-6 select-none"
          style={{ color: "#7C3AED" }}
        >
          {BESH_ASCII.join("\n")}
        </pre>

        {/* Title */}
        <h1 className="font-display text-4xl sm:text-5xl md:text-7xl font-black tracking-[0.2em] uppercase mb-4 neon-glow text-[#A78BFA]">
          besh
        </h1>

        {/* Tagline with typewriter */}
        <p
          ref={taglineRef}
          className="font-mono text-sm sm:text-base md:text-lg text-[#00FF41] mb-8 tracking-wide"
        >
          {">"} {TAGLINE}
        </p>

        {/* Tech badges */}
        <div className="flex flex-wrap justify-center gap-3 mb-12">
          {["C", "GCC", "POSIX", "AST", "Lexer", "Parser"].map((tag) => (
            <span
              key={tag}
              className="px-3 py-1 text-xs font-mono border rounded-sm"
              style={{
                borderColor: "#4C1D95",
                color: "#A78BFA",
                background: "rgba(76,29,149,0.15)",
              }}
            >
              {tag}
            </span>
          ))}
        </div>

        {/* Scroll CTA */}
        <div ref={ctaRef} className="animate-float">
          <div className="text-[#7C3AED]/60 text-xs font-mono tracking-widest uppercase mb-2">
            scroll to explore
          </div>
          <div className="w-px h-8 mx-auto bg-gradient-to-b from-[#7C3AED] to-transparent" />
        </div>
      </div>
    </section>
  );
}
```

- [ ] **Step 2: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add Hero section with ASCII logo and tagline"
```

---

### Task 9: Builtins Grid + Feature Section

**Files:**
- Create: `app/components/BuiltinsGrid.tsx`
- Create: `app/components/FeatureSection.tsx`

**Interfaces:**
- Consumes: `GlitchCard`
- Produces: `<BuiltinsGrid />` — grid of 29 builtin command cards

- [ ] **Step 1: Write BuiltinsGrid**

Write `app/components/BuiltinsGrid.tsx`:

```tsx
"use client";
import GlitchCard from "./GlitchCard";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";

const BUILTINS = [
  { name: "echo", desc: "Print to stdout" },
  { name: "cd", desc: "Change directory" },
  { name: "pwd", desc: "Print working dir" },
  { name: "ls", desc: "List directory (via exec)" },
  { name: "export", desc: "Set env variables" },
  { name: "unset", desc: "Remove variables" },
  { name: "alias", desc: "Define aliases" },
  { name: "unalias", desc: "Remove aliases" },
  { name: "source", desc: "Execute script file" },
  { name: "exit", desc: "Exit the shell" },
  { name: "jobs", desc: "List background jobs" },
  { name: "fg", desc: "Foreground job" },
  { name: "bg", desc: "Background job" },
  { name: "history", desc: "Command history" },
  { name: "read", desc: "Read from stdin" },
  { name: "test/[", desc: "Conditional eval" },
  { name: "true", desc: "Return 0" },
  { name: "false", desc: "Return 1" },
  { name: "exec", desc: "Replace process" },
  { name: "shift", desc: "Shift positional params" },
  { name: "times", desc: "Process times" },
  { name: "trap", desc: "Signal handler" },
  { name: "umask", desc: "File creation mask" },
  { name: "break", desc: "Exit loop" },
  { name: "continue", desc: "Next iteration" },
  { name: "return", desc: "Return from function" },
  { name: "set", desc: "Shell options" },
  { name: "type", desc: "Command type info" },
  { name: "help", desc: "Builtin help" },
];

const COLORS = ["#7C3AED", "#A78BFA", "#F43F5E", "#00FF41"];

export default function BuiltinsGrid() {
  const isDesktop = useIsDesktop();

  return (
    <section id="builtins" className="relative z-10 py-20 px-4 max-w-7xl mx-auto">
      <div className="text-center mb-12">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /builtins
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          29 built-in commands powering the shell experience
        </p>
      </div>

      <div className={`grid gap-3 ${isDesktop ? "grid-cols-4 md:grid-cols-6" : "grid-cols-2"}`}>
        {BUILTINS.map((cmd, i) => (
          <GlitchCard
            key={cmd.name}
            title={cmd.name}
            subtitle={cmd.desc}
            glowColor={COLORS[i % COLORS.length]}
          />
        ))}
      </div>
    </section>
  );
}
```

- [ ] **Step 2: Write FeatureSection**

Write `app/components/FeatureSection.tsx`:

```tsx
import GlitchCard from "./GlitchCard";

const FEATURES = [
  { title: "Lexer + Parser", desc: "Hand-crafted recursive-descent parser with full POSIX grammar support — pipelines, lists, subshells, functions." },
  { title: "Job Control", desc: "Background jobs with fg/bg, process groups, SIGCHLD handling, and `jobs` builtin." },
  { title: "Built-in Line Editor", desc: "Zero-dependency readline replacement — raw terminal I/O, history, tab completion, colored prompt." },
  { title: "Expansion Engine", desc: "Variable expansion with ${VAR:-default}, arithmetic $((1+1)), command substitution $(cmd), globbing, tilde." },
  { title: "Here-Documents", desc: "<< DELIM with variable expansion, quoted delimiter support, and tab-stripping variant <<- ." },
  { title: "Control Flow", desc: "if/elif/else/fi, for/while/until loops, case/esac, break/continue, function definitions." },
];

export default function FeatureSection() {
  return (
    <section id="features" className="relative z-10 py-20 px-4 max-w-6xl mx-auto">
      <div className="text-center mb-12">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /features
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          A terminal emulator that doesn&#39;t just look like one — it thinks like one
        </p>
      </div>

      <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
        {FEATURES.map((f) => (
          <GlitchCard key={f.title} title={f.title} subtitle={f.desc} />
        ))}
      </div>
    </section>
  );
}
```

- [ ] **Step 3: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add BuiltinsGrid and FeatureSection components"
```

---

### Task 10: AST Visualizer

**Files:**
- Create: `app/components/AstVisualizer.tsx`

**Interfaces:**
- Produces: `<AstVisualizer />` — desktop shows rotating 3D tree via R3F; mobile shows static 2D tree

- [ ] **Step 1: Write AstVisualizer**

Write `app/components/AstVisualizer.tsx`:

```tsx
"use client";
import { useRef, useState } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import { Text3D, OrbitControls } from "@react-three/drei";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

function TreeNode({ label, position, color }: { label: string; position: [number, number, number]; color: string }) {
  return (
    <mesh position={position}>
      <boxGeometry args={[1.2, 0.4, 0.4]} />
      <meshStandardMaterial color={color} emissive={color} emissiveIntensity={0.5} />
    </mesh>
  );
}

function Edge({ from, to }: { from: [number, number, number]; to: [number, number, number] }) {
  return (
    <line>
      <bufferGeometry>
        <float32BufferAttribute
          attach="attributes-position"
          args={[new Float32Array([...from, ...to]), 3]}
          count={2}
        />
      </bufferGeometry>
      <lineBasicMaterial color="#4C1D95" transparent opacity={0.5} />
    </line>
  );
}

function AstScene() {
  const groupRef = useRef<THREE.Group>(null);
  const reduced = useReducedMotion();

  useFrame(() => {
    if (groupRef.current && !reduced) {
      groupRef.current.rotation.y += 0.003;
    }
  });

  const nodes = [
    { label: "NODE_LIST", pos: [0, 2, 0] as [number, number, number], color: "#7C3AED" },
    { label: "CMD(cat)", pos: [-1.5, 0, 0] as [number, number, number], color: "#00FF41" },
    { label: "CMD(echo)", pos: [1.5, 0, 0] as [number, number, number], color: "#F43F5E" },
    { label: "REDIR(<<HD)", pos: [-2.5, -2, 0] as [number, number, number], color: "#A78BFA" },
    { label: "ARG(done)", pos: [1.5, -2, 0] as [number, number, number], color: "#F43F5E" },
  ];

  const edges = [
    { from: [0, 1.7, 0] as [number, number, number], to: [-1.5, 0.2, 0] as [number, number, number] },
    { from: [0, 1.7, 0] as [number, number, number], to: [1.5, 0.2, 0] as [number, number, number] },
    { from: [-1.5, -0.2, 0] as [number, number, number], to: [-2.5, -1.8, 0] as [number, number, number] },
    { from: [1.5, -0.2, 0] as [number, number, number], to: [1.5, -1.8, 0] as [number, number, number] },
  ];

  return (
    <group ref={groupRef}>
      <ambientLight intensity={0.3} />
      <pointLight position={[5, 5, 5]} intensity={1} color="#7C3AED" />
      {nodes.map((n) => (
        <TreeNode key={n.label} {...n} />
      ))}
      {edges.map((e, i) => (
        <Edge key={i} {...e} />
      ))}
    </group>
  );
}

export default function AstVisualizer() {
  const isDesktop = useIsDesktop();

  return (
    <section className="relative z-10 py-20 px-4 max-w-5xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /ast
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          Every command becomes a node in an Abstract Syntax Tree
        </p>
      </div>

      {isDesktop ? (
        <div className="w-full h-80 md:h-96 neon-border rounded-sm overflow-hidden">
          <Canvas camera={{ position: [0, 0, 8], fov: 50 }} dpr={[1, 1.5]}>
            <AstScene />
            <OrbitControls enableZoom={false} enablePan={false} />
          </Canvas>
        </div>
      ) : (
        <div className="neon-border rounded-sm p-6 bg-[#0F0F23]/80">
          <pre className="font-mono text-xs md:text-sm text-[#00FF41] overflow-x-auto">
{`NODE_LIST
├── CMD(cat)
│   └── REDIR(<<HD)
└── CMD(echo)
    └── ARG(done)`}
          </pre>
        </div>
      )}
    </section>
  );
}
```

- [ ] **Step 2: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add AST visualizer with 3D R3F tree"
```

---

### Task 11: Mini-Games (PacEat + SpeedType + AsciiRain + TowerDefense)

**Files:**
- Create: `app/games/PacEat.tsx`
- Create: `app/games/SpeedType.tsx`
- Create: `app/games/AsciiRain.tsx`
- Create: `app/games/TowerDefense.tsx`

**Interfaces:**
- Consumes: `useIsDesktop()`, `useReducedMotion()`
- Produces: Four mini-game components

- [ ] **Step 1: Write PacEat**

Write `app/games/PacEat.tsx`:

```tsx
"use client";
import { useRef, useEffect, useState, useCallback } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

const COMMANDS = "echo cd pwd ls cat export unset alias source exit jobs fg bg history read test help".split(" ");

interface Ghost { x: number; y: number; dx: number; dy: number; color: string; }

export default function PacEat() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [score, setScore] = useState(0);
  const [gameOver, setGameOver] = useState(false);
  const pacmanRef = useRef({ x: 200, y: 200, dir: 1, mouth: 0 });
  const foodsRef = useRef<{ x: number; y: number; cmd: string }[]>([]);
  const ghostsRef = useRef<Ghost[]>([]);
  const animRef = useRef<number>(0);

  const reset = useCallback(() => {
    pacmanRef.current = { x: 200, y: 200, dir: 1, mouth: 0 };
    foodsRef.current = Array.from({ length: 15 }, () => ({
      x: 50 + Math.random() * 300,
      y: 50 + Math.random() * 250,
      cmd: COMMANDS[Math.floor(Math.random() * COMMANDS.length)],
    }));
    ghostsRef.current = [
      { x: 50, y: 50, dx: 1.5, dy: 1, color: "#F43F5E" },
      { x: 350, y: 250, dx: -1, dy: -1.5, color: "#A78BFA" },
    ];
    setScore(0);
    setGameOver(false);
  }, []);

  useEffect(() => {
    reset();
  }, [reset]);

  useEffect(() => {
    if (!isDesktop || reduced || gameOver) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const W = 400, H = 300;

    let keys: Record<string, boolean> = {};
    const onKey = (e: KeyboardEvent) => { keys[e.key] = true; e.preventDefault(); };
    const offKey = (e: KeyboardEvent) => { keys[e.key] = false; };
    window.addEventListener("keydown", onKey);
    window.addEventListener("keyup", offKey);

    const loop = () => {
      const p = pacmanRef.current;
      if (keys["ArrowLeft"]) { p.x -= 3; p.dir = -1; }
      if (keys["ArrowRight"]) { p.x += 3; p.dir = 1; }
      if (keys["ArrowUp"]) p.y -= 3;
      if (keys["ArrowDown"]) p.y += 3;
      p.x = Math.max(15, Math.min(W - 15, p.x));
      p.y = Math.max(15, Math.min(H - 15, p.y));
      p.mouth = (p.mouth + 0.15) % 1;

      ghostsRef.current.forEach((g) => {
        g.x += g.dx;
        g.y += g.dy;
        if (g.x < 15 || g.x > W - 15) g.dx *= -1;
        if (g.y < 15 || g.y > H - 15) g.dy *= -1;
      });

      foodsRef.current = foodsRef.current.filter((f) => {
        const dist = Math.hypot(p.x - f.x, p.y - f.y);
        if (dist < 18) { setScore((s) => s + 10); return false; }
        return true;
      });

      for (const g of ghostsRef.current) {
        if (Math.hypot(p.x - g.x, p.y - g.y) < 20) { setGameOver(true); return; }
      }

      if (foodsRef.current.length === 0) { setGameOver(true); return; }

      ctx.fillStyle = "#0F0F23";
      ctx.fillRect(0, 0, W, H);
      ctx.strokeStyle = "#4C1D95";
      ctx.strokeRect(0, 0, W, H);

      foodsRef.current.forEach((f) => {
        ctx.fillStyle = "#00FF41";
        ctx.beginPath();
        ctx.arc(f.x, f.y, 6, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = "#A78BFA";
        ctx.font = "8px monospace";
        ctx.fillText(f.cmd, f.x - 12, f.y - 10);
      });

      ghostsRef.current.forEach((g) => {
        ctx.fillStyle = g.color;
        ctx.beginPath();
        ctx.arc(g.x, g.y, 12, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = "#FFF";
        ctx.beginPath();
        ctx.arc(g.x - 4, g.y - 3, 3, 0, Math.PI * 2);
        ctx.fill();
        ctx.beginPath();
        ctx.arc(g.x + 4, g.y - 3, 3, 0, Math.PI * 2);
        ctx.fill();
      });

      ctx.fillStyle = "#FBBF24";
      ctx.beginPath();
      const angle = p.mouth * 0.5;
      ctx.arc(p.x, p.y, 14, p.dir > 0 ? angle : Math.PI + angle, p.dir > 0 ? Math.PI * 2 - angle : Math.PI - angle);
      ctx.lineTo(p.x, p.y);
      ctx.fill();

      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);

    return () => {
      cancelAnimationFrame(animRef.current);
      window.removeEventListener("keydown", onKey);
      window.removeEventListener("keyup", offKey);
    };
  }, [isDesktop, reduced, gameOver]);

  if (!isDesktop) {
    return (
      <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto text-center">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/pac-eat</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60">Open on desktop to play Pac-Eat: commands edition</p>
      </section>
    );
  }

  return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/pac-eat</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 mb-2">Eat the commands! Avoid the ghosts! Arrow keys to move.</p>
        <p className="font-mono text-lg text-[#00FF41]">Score: {score}</p>
      </div>
      <div className="flex justify-center">
        <canvas ref={canvasRef} width={400} height={300} className="neon-border rounded-sm" />
      </div>
      {gameOver && (
        <div className="text-center mt-4">
          <p className="font-mono text-xl text-[#F43F5E] mb-2">
            {foodsRef.current.length === 0 ? "YOU WIN!" : "GAME OVER"}
          </p>
          <button
            onClick={reset}
            className="px-4 py-2 font-mono text-sm text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors"
          >
            [restart]
          </button>
        </div>
      )}
    </section>
  );
}
```

- [ ] **Step 2: Write SpeedType**

Write `app/games/SpeedType.tsx`:

```tsx
"use client";
import { useState, useEffect, useCallback, useRef } from "react";

const WORDS = "echo cd pwd ls cat grep sed awk export unset alias source exit jobs fg bg history set read test true false exec shift times trap umask break continue return help type".split(" ");

export default function SpeedType() {
  const [current, setCurrent] = useState("");
  const [target, setTarget] = useState("");
  const [score, setScore] = useState(0);
  const [time, setTime] = useState(30);
  const [playing, setPlaying] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const newWord = useCallback(() => {
    setCurrent(WORDS[Math.floor(Math.random() * WORDS.length)]);
  }, []);

  const start = useCallback(() => {
    setPlaying(true);
    setScore(0);
    setTime(30);
    setTarget("");
    newWord();
    inputRef.current?.focus();
  }, [newWord]);

  useEffect(() => {
    if (!playing || time <= 0) return;
    const t = setInterval(() => setTime((p) => {
      if (p <= 1) { setPlaying(false); return 0; }
      return p - 1;
    }), 1000);
    return () => clearInterval(t);
  }, [playing, time]);

  const handleChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const val = e.target.value;
    setTarget(val);
    if (val === current) {
      setScore((s) => s + 1);
      setTarget("");
      newWord();
    }
  }, [current, newWord]);

  return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/speed-type</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 mb-2">Type the commands as fast as you can!</p>
      </div>

      <div className="neon-border rounded-sm p-8 max-w-md mx-auto text-center bg-[#0F0F23]/80">
        {!playing ? (
          <div>
            {time === 0 && <p className="font-mono text-xl text-[#F43F5E] mb-4">Time&apos;s up! Score: {score}</p>}
            <button onClick={start} className="px-6 py-3 font-mono text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">
              {time === 0 ? "[retry]" : "[start]"}
            </button>
          </div>
        ) : (
          <div>
            <div className="flex justify-between font-mono text-sm mb-6">
              <span className="text-[#A78BFA]">Score: {score}</span>
              <span className="text-[#F43F5E]">Time: {time}s</span>
            </div>

            <div className="font-mono text-3xl text-[#00FF41] mb-4 neon-glow-green tracking-wider">
              {current}
            </div>

            <input
              ref={inputRef}
              value={target}
              onChange={handleChange}
              className="w-full px-4 py-2 bg-transparent border border-[#4C1D95] rounded text-[#A78BFA] font-mono text-center text-lg outline-none focus:border-[#7C3AED] transition-colors"
              autoFocus
              spellCheck={false}
              autoComplete="off"
            />
          </div>
        )}
      </div>
    </section>
  );
}
```

- [ ] **Step 3: Write AsciiRain**

Write `app/games/AsciiRain.tsx`:

```tsx
"use client";
import { useRef, useEffect, useState, useCallback } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface Drop { x: number; y: number; speed: number; char: string; length: number; chars: string[]; }

const CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$#@%&*()_+-=[]{}|;:<>?,./\\";

export default function AsciiRain() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [input, setInput] = useState("besh");
  const [density, setDensity] = useState(30);
  const animRef = useRef<number>(0);

  useEffect(() => {
    if (!isDesktop || reduced) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    let drops: Drop[] = [];
    const init = () => {
      const chars = input || CHARS;
      drops = Array.from({ length: density }, () => ({
        x: Math.random() * canvas.width,
        y: Math.random() * canvas.height,
        speed: 1 + Math.random() * 4,
        char: chars[Math.floor(Math.random() * chars.length)],
        length: 10 + Math.floor(Math.random() * 20),
        chars: Array.from({ length: 10 + Math.floor(Math.random() * 20) }, () => chars[Math.floor(Math.random() * chars.length)]),
      }));
    };
    init();

    const loop = () => {
      ctx.fillStyle = "rgba(15, 15, 35, 0.1)";
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      for (const d of drops) {
        for (let i = 0; i < d.chars.length; i++) {
          const alpha = 1 - i / d.chars.length;
          ctx.fillStyle = i === 0 ? `rgba(167, 139, 250, ${alpha})` : `rgba(0, 255, 65, ${alpha * 0.5})`;
          ctx.font = "14px monospace";
          ctx.fillText(d.chars[i], d.x, d.y - i * 16);
        }
        d.y += d.speed;
        if (d.y > canvas.height + d.chars.length * 16) {
          d.y = -d.chars.length * 16;
          d.x = Math.random() * canvas.width;
        }
      }
      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);

    return () => { cancelAnimationFrame(animRef.current); };
  }, [isDesktop, reduced, input, density]);

  return (
    <section className="relative z-10 py-20 px-4 max-w-5xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/ascii-rain</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60">Customize your own matrix rain</p>
      </div>

      <div className="flex flex-wrap justify-center gap-4 mb-6">
        <input
          value={input}
          onChange={(e) => setInput(e.target.value || " ")}
          className="px-4 py-2 bg-[#0F0F23] border border-[#4C1D95] rounded text-[#00FF41] font-mono text-sm outline-none focus:border-[#7C3AED]"
          placeholder="characters..."
          maxLength={20}
        />
        <select
          value={density}
          onChange={(e) => setDensity(Number(e.target.value))}
          className="px-4 py-2 bg-[#0F0F23] border border-[#4C1D95] rounded text-[#A78BFA] font-mono text-sm outline-none"
        >
          <option value={15}>Sparse</option>
          <option value={30}>Normal</option>
          <option value={60}>Dense</option>
          <option value={100}>Storm</option>
        </select>
      </div>

      {isDesktop && !reduced ? (
        <canvas ref={canvasRef} width={600} height={350} className="w-full max-w-2xl mx-auto neon-border rounded-sm" />
      ) : (
        <div className="neon-border rounded-sm p-6 max-w-2xl mx-auto bg-[#0F0F23]/80">
          <p className="font-mono text-sm text-[#A78BFA]/60 text-center">
            Matrix rain generator — open on desktop for full interactive experience
          </p>
        </div>
      )}
    </section>
  );
}
```

- [ ] **Step 4: Write TowerDefense**

Write `app/games/TowerDefense.tsx`:

```tsx
"use client";
import { useRef, useEffect, useState, useCallback } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface Enemy { x: number; y: number; hp: number; speed: number; }
interface Tower { x: number; y: number; }
interface Bullet { x: number; y: number; dx: number; dy: number; target: number; }

export default function TowerDefense() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [score, setScore] = useState(0);
  const [lives, setLives] = useState(10);
  const [gameOver, setGameOver] = useState(false);
  const towersRef = useRef<Tower[]>([]);
  const enemiesRef = useRef<Enemy[]>([]);
  const bulletsRef = useRef<Bullet[]>([]);
  const animRef = useRef<number>(0);
  const spawnRef = useRef(0);

  const reset = useCallback(() => {
    towersRef.current = [{ x: 300, y: 180 }];
    enemiesRef.current = [];
    bulletsRef.current = [];
    spawnRef.current = 0;
    setScore(0);
    setLives(10);
    setGameOver(false);
  }, []);

  useEffect(() => {
    reset();
  }, [reset]);

  useEffect(() => {
    if (!isDesktop || reduced || gameOver) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const loop = () => {
      const W = canvas.width, H = canvas.height;

      // Spawn
      if (spawnRef.current <= 0) {
        enemiesRef.current.push({
          x: Math.random() * W,
          y: -20,
          hp: 3,
          speed: 1 + Math.random() * 2,
        });
        spawnRef.current = 60;
      }
      spawnRef.current--;

      // Update enemies
      for (const e of enemiesRef.current) {
        e.y += e.speed;
      }

      // Towers shoot
      for (const t of towersRef.current) {
        let closest = -1;
        let minDist = Infinity;
        for (let i = 0; i < enemiesRef.current.length; i++) {
          const dist = Math.hypot(t.x - enemiesRef.current[i].x, t.y - enemiesRef.current[i].y);
          if (dist < minDist) { minDist = dist; closest = i; }
        }
        if (closest >= 0 && minDist < 200) {
          const e = enemiesRef.current[closest];
          const angle = Math.atan2(e.y - t.y, e.x - t.x);
          bulletsRef.current.push({ x: t.x, y: t.y, dx: Math.cos(angle) * 5, dy: Math.sin(angle) * 5, target: closest });
        }
      }

      // Update bullets
      for (let b = bulletsRef.current.length - 1; b >= 0; b--) {
        bulletsRef.current[b].x += bulletsRef.current[b].dx;
        bulletsRef.current[b].y += bulletsRef.current[b].dy;
        if (bulletsRef.current[b].x < 0 || bulletsRef.current[b].x > W ||
            bulletsRef.current[b].y < 0 || bulletsRef.current[b].y > H) {
          bulletsRef.current.splice(b, 1);
        }
      }

      // Hit detection
      for (let b = bulletsRef.current.length - 1; b >= 0; b--) {
        for (let e = enemiesRef.current.length - 1; e >= 0; e--) {
          if (Math.hypot(bulletsRef.current[b].x - enemiesRef.current[e].x, bulletsRef.current[b].y - enemiesRef.current[e].y) < 15) {
            enemiesRef.current[e].hp--;
            bulletsRef.current.splice(b, 1);
            if (enemiesRef.current[e].hp <= 0) {
              enemiesRef.current.splice(e, 1);
              setScore((s) => s + 100);
            }
            break;
          }
        }
      }

      // Enemies reach bottom
      for (let e = enemiesRef.current.length - 1; e >= 0; e--) {
        if (enemiesRef.current[e].y > H) {
          enemiesRef.current.splice(e, 1);
          setLives((l) => {
            if (l <= 1) { setGameOver(true); return 0; }
            return l - 1;
          });
        }
      }

      // Draw
      ctx.fillStyle = "#0F0F23";
      ctx.fillRect(0, 0, W, H);
      ctx.strokeStyle = "#4C1D95";
      ctx.strokeRect(0, 0, W, H);

      for (const t of towersRef.current) {
        ctx.fillStyle = "#7C3AED";
        ctx.fillRect(t.x - 12, t.y - 12, 24, 24);
        ctx.strokeStyle = "#A78BFA";
        ctx.strokeRect(t.x - 12, t.y - 12, 24, 24);
      }

      for (const e of enemiesRef.current) {
        ctx.fillStyle = "#F43F5E";
        ctx.beginPath();
        ctx.arc(e.x, e.y, 12, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = "#FFF";
        ctx.font = "10px monospace";
        ctx.fillText(String(e.hp), e.x - 3, e.y + 4);
      }

      for (const b of bulletsRef.current) {
        ctx.fillStyle = "#00FF41";
        ctx.beginPath();
        ctx.arc(b.x, b.y, 4, 0, Math.PI * 2);
        ctx.fill();
      }

      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(animRef.current);
  }, [isDesktop, reduced, gameOver]);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return;
    const x = ((e.clientX - rect.left) / rect.width) * 600;
    const y = ((e.clientY - rect.top) / rect.height) * 400;
    if (towersRef.current.length < 5) {
      towersRef.current.push({ x, y });
    }
  }, []);

  if (!isDesktop) {
    return (
      <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto text-center">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/tower-defense</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60">Open on desktop to play: defend against rogue processes!</p>
      </section>
    );
  }

  return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/tower-defense</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 mb-2">Click to place towers. Defend against rogue processes!</p>
        <div className="flex justify-center gap-6 font-mono text-sm">
          <span className="text-[#00FF41]">Score: {score}</span>
          <span className="text-[#F43F5E]">Lives: {"█".repeat(lives)}{"░".repeat(10 - lives)}</span>
        </div>
      </div>
      <div className="flex justify-center">
        <canvas ref={canvasRef} width={600} height={400} className="neon-border rounded-sm cursor-crosshair" onClick={handleCanvasClick} />
      </div>
      {gameOver && (
        <div className="text-center mt-4">
          <p className="font-mono text-xl text-[#F43F5E] mb-2">SYSTEM COMPROMISED</p>
          <button onClick={reset} className="px-4 py-2 font-mono text-sm text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10">[reboot]</button>
        </div>
      )}
    </section>
  );
}
```

- [ ] **Step 5: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add all four mini-games (PacEat, SpeedType, AsciiRain, TowerDefense)"
```

---

### Task 12: Benchmark Bar + Footer

**Files:**
- Create: `app/components/BenchmarkBar.tsx`
- Create: `app/components/Footer.tsx`

- [ ] **Step 1: Write BenchmarkBar**

Write `app/components/BenchmarkBar.tsx`:

```tsx
"use client";
import { useEffect, useState, useRef } from "react";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface Stat { label: string; value: number; suffix: string; }

const STATS: Stat[] = [
  { label: "Lines of C", value: 3400, suffix: "" },
  { label: "Builtins", value: 29, suffix: "" },
  { label: "Tokens/sec", value: 98420, suffix: "" },
  { label: "Parse Speed", value: 1.2, suffix: "ms" },
  { label: "Binary Size", value: 156, suffix: "KB" },
  { label: "Dependencies", value: 0, suffix: "" },
];

function AnimatedCounter({ value, suffix, animate }: { value: number; suffix: string; animate: boolean }) {
  const [display, setDisplay] = useState(0);
  const ref = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    if (!animate) { setDisplay(value); return; }
    let start = 0;
    const duration = 1500;
    const step = Math.ceil(value / (duration / 16));
    const timer = setInterval(() => {
      start += step;
      if (start >= value) { setDisplay(value); clearInterval(timer); }
      else setDisplay(start);
    }, 16);
    return () => clearInterval(timer);
  }, [value, animate]);

  return <span ref={ref}>{display.toLocaleString()}{suffix}</span>;
}

export default function BenchmarkBar() {
  const reduced = useReducedMotion();

  return (
    <section className="relative z-10 py-20 px-4 max-w-6xl mx-auto">
      <div className="text-center mb-12">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/benchmark</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60">Pseudo-benchmarks. Because every shell needs a spec sheet.</p>
      </div>

      <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-4">
        {STATS.map((stat) => (
          <div key={stat.label} className="neon-border rounded-sm p-4 text-center bg-[#0F0F23]/80">
            <div className="font-display text-2xl md:text-3xl font-black text-[#00FF41] neon-glow-green mb-1">
              <AnimatedCounter value={stat.value} suffix={stat.suffix} animate={!reduced} />
            </div>
            <div className="font-mono text-xs text-[#A78BFA]/60 uppercase tracking-wider">{stat.label}</div>
          </div>
        ))}
      </div>
    </section>
  );
}
```

- [ ] **Step 2: Write Footer**

Write `app/components/Footer.tsx`:

```tsx
export default function Footer() {
  return (
    <footer className="relative z-10 py-12 px-4 border-t border-[#4C1D95]">
      <div className="max-w-5xl mx-auto">
        <div className="flex flex-col md:flex-row justify-between items-center gap-4 mb-6">
          <div className="font-display text-lg font-black tracking-[0.2em] text-[#7C3AED] neon-glow">
            besh
          </div>
          <div className="flex gap-6 font-mono text-xs text-[#A78BFA]/60">
            <a href="#" className="hover:text-[#00FF41] transition-colors">[src]</a>
            <a href="#" className="hover:text-[#00FF41] transition-colors">[docs]</a>
            <a href="#" className="hover:text-[#00FF41] transition-colors">[license]</a>
            <a href="#" className="hover:text-[#00FF41] transition-colors">[contribute]</a>
          </div>
        </div>

        <div className="text-center font-mono text-xs text-[#4C1D95]">
          <p>besh v1.0.0 — a bash-compatible shell written in C</p>
          <p className="mt-1">Built with 3400 lines of pure C, zero dependencies, and a lot of 80&#39;s terminal nostalgia.</p>
          <p className="mt-3 text-[#00FF41]/30">
            ████████ READY ████████
          </p>
        </div>
      </div>
    </footer>
  );
}
```

- [ ] **Step 3: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: add BenchmarkBar and Footer components"
```

---

### Task 13: Page Composition — Wire Everything Together

**Files:**
- Modify: `app/page.tsx`
- Modify: `app/layout.tsx` (final metadata)

**Interfaces:**
- Consumes: All components from Tasks 5–12
- Produces: Complete single-page app

- [ ] **Step 1: Write page.tsx**

Replace `app/page.tsx`:

```tsx
import Hero from "./components/Hero";
import Terminal from "./components/Terminal";
import FeatureSection from "./components/FeatureSection";
import BuiltinsGrid from "./components/BuiltinsGrid";
import AstVisualizer from "./components/AstVisualizer";
import BenchmarkBar from "./components/BenchmarkBar";
import PacEat from "./games/PacEat";
import SpeedType from "./games/SpeedType";
import AsciiRain from "./games/AsciiRain";
import TowerDefense from "./games/TowerDefense";
import Footer from "./components/Footer";

export default function Home() {
  return (
    <main className="relative">
      <Hero />
      <Terminal />
      <FeatureSection />
      <BuiltinsGrid />
      <PacEat />
      <TowerDefense />
      <AstVisualizer />
      <AsciiRain />
      <SpeedType />
      <BenchmarkBar />
      <Footer />
    </main>
  );
}
```

- [ ] **Step 2: Finalize layout.tsx metadata**

Modify `app/layout.tsx`:

```tsx
import type { Metadata } from "next";
import HUDOverlay from "./components/HUDOverlay";
import "./globals.css";

export const metadata: Metadata = {
  title: "besh — Cyber Shell",
  description: "besh: a bash-compatible Unix shell written in C. Full-featured shell with job control, line editing, pipelines, redirections, and 29 built-in commands.",
  keywords: ["shell", "bash", "unix", "terminal", "c", "cyberpunk"],
  openGraph: {
    title: "besh — Cyber Shell",
    description: "A bash-compatible shell written in C",
  },
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className="dark">
      <body className="bg-[#0F0F23] text-[#E2E8F0] font-mono antialiased">
        {children}
        <HUDOverlay />
      </body>
    </html>
  );
}
```

- [ ] **Step 3: Full build check**

```bash
cd /root/001/web && npm run build 2>&1 | tail -20
```
Expected: Build succeeds with no errors.

- [ ] **Step 4: Verify dev server renders all sections**

```bash
cd /root/001/web && timeout 15 npm run dev 2>&1 || true
```

- [ ] **Step 5: Commit**

```bash
cd /root/001 && git add web/ && git commit -m "feat: compose all sections into single-page cyber terminal landing"
```

---

### Task 14: Final Polish — Mobile Testing + Reduced Motion

**Files:**
- Modify: `tailwind.config.ts` (add responsive utilities if needed)
- Modify: `app/globals.css` (mobile tweaks)

**Note:** This task verifies mobile responsiveness at 375px, 768px, 1024px, 1440px breakpoints and reduced-motion behavior. Fix any layout issues found during testing.

- [ ] **Step 1: Test at 375px viewport**

```bash
cd /root/001/web && timeout 10 npm run dev 2>&1 || true
```
Open http://localhost:3000, set viewport to 375px wide. Verify:
- No horizontal scroll
- Hero ASCII logo readable
- Terminal has quick command buttons
- Builtins grid is 2 columns
- Games show "open on desktop" message
- Footer text wraps properly
- All text >= 16px body

- [ ] **Step 2: Test at 768px (tablet)**

```bash
# Same dev server
```
Verify:
- Hero scales up
- Builtins grid shows 3-4 columns
- Terminal larger
- No layout breaks

- [ ] **Step 3: Test reduced motion**

Set `prefers-reduced-motion: reduce` in dev tools. Verify:
- No CRT flicker animation
- No matrix rain
- No glitch hover effects
- No animated counters (static numbers)
- No floating scroll CTA

- [ ] **Step 4: Run production build**

```bash
cd /root/001/web && npm run build 2>&1
```
Expected: All pages built successfully, no warnings.

- [ ] **Step 5: Commit final polish**

```bash
cd /root/001 && git add web/ && git commit -m "polish: mobile responsive tweaks and reduced-motion support"
```
