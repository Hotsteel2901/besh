# besh Cyberpunk Terminal Landing Page — Design Spec

## Summary
A maximalist cyberpunk-themed landing page for the **besh** shell project. Cyberpunk terminal aesthetics (neon, CRT, glitch, matrix rain). Desktop-first with progressive enhancement for mobile. Includes interactive mini-games and heavy visual effects as decorative embellishments.

## Tech Stack
- **Next.js 14 App Router** + TypeScript
- **Tailwind CSS** + custom cyberpunk theme
- **GSAP** (ScrollTrigger, TextPlugin, Flip)
- **Three.js / React Three Fiber** — desktop 3D particle background
- **Canvas API** — mini-game rendering

## Color Tokens
| Token | Hex | Role |
|-------|-----|------|
| neon-purple | #7C3AED | Primary, buttons, links |
| violet | #A78BFA | Secondary, gradients |
| accent-rose | #F43F5E | CTA, destructive |
| void-bg | #0F0F23 | Global background |
| terminal-green | #00FF41 | Terminal text, code |
| dark-panel | #1A1030 | Cards, panels |
| muted-border | #4C1D95 | Borders, separators |
| foreground | #E2E8F0 | Body text |

## Typography
- **Orbitron** (700/900) — Display headings, navigation
- **JetBrains Mono** (400/500) — Terminal output, code, data

## Architecture
```
/app
  layout.tsx          — Global CRT overlay + scanlines + font loading
  page.tsx            — Main page composing all sections
  /components
    MatrixRain.tsx     — 3D particle rain (desktop) / CSS rain (mobile)
    Terminal.tsx       — Interactive terminal emulator
    GlitchCard.tsx     — Neon card with hover glitch effect
    HUDOverlay.tsx     — Global scanlines + corner brackets + CRT flicker
    AstVisualizer.tsx  — Pseudo 3D AST tree visualization
    BenchmarkBar.tsx   — Animated number counter dashboard
    Footer.tsx         — Neon tube footer
  /games
    PacEat.tsx         — Pac-Man eating commands (desktop only)
    TowerDefense.tsx   — Command tower defense (desktop only)
    SpeedType.tsx      — Typing speed race (responsive)
    AsciiRain.tsx      — Interactive character rain (responsive)
  /hooks
    useReducedMotion.ts
    useIsDesktop.ts    — >=1024px check
    useTerminal.ts     — Terminal state management
```

## 10 Sections
1. **Hero** — Giant besh ASCII logo + typing animation + matrix rain BG
2. **Feature Terminal** — Interactive terminal emulator
3. **Builtins Grid** — 29 builtin commands as neon cards
4. **Pac-Eat** — Pac-Man mini-game (desktop only)
5. **Tower Defense** — Simple tower defense (desktop only)
6. **AstVisualizer** — AST tree 3D visualization
7. **Ascii Rain** — Interactive character rain generator
8. **Speed Type** — Typing speed race game
9. **Benchmark** — Pseudo benchmark dashboard
10. **Footer** — Neon tube footer

## Mobile Strategy (Progressive Enhancement)
- **Desktop (>=1024px)**: Full 3D, full games, full GSAP animations
- **Mobile (<1024px)**: 2D/CSS only, simplified games or "open on desktop" prompt, reduced animations
- `prefers-reduced-motion` respected globally
- All touch targets >= 44px
- Minimum body text 16px on mobile

## Constraints
- Single-page, no routing needed (use single `page.tsx` with all sections)
- No external APIs or databases
- No auth
- All games and effects are client-side only
- Must work offline after initial load

## Success Criteria
- All 10 sections render correctly
- Desktop has full 3D + animations
- Mobile shows simplified versions gracefully
- Terminal emulator accepts real shell-like input with pre-programmed responses
- Mini-games are playable and fun
- Visual design is unmistakably cyberpunk
