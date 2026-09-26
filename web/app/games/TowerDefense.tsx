"use client";
import { useRef, useEffect, useState, useCallback } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";
import { useInViewport } from "@/app/hooks/useInViewport";
import GameDesktopCard from "@/app/components/GameDesktopCard";
import GameHeader from "@/app/components/GameHeader";

interface Enemy { x: number; y: number; hp: number; maxHp: number; speed: number; }
interface Tower { x: number; y: number; }

const RANGE = 200;       // tower firing radius
const MAX_TOWERS = 5;
const MIN_TOWER_GAP = 45; // minimum spacing between towers

export default function TowerDefense() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const { ref: sectionRef, inView } = useInViewport<HTMLElement>();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [score, setScore] = useState(0);
  const [lives, setLives] = useState(10);
  const [gameOver, setGameOver] = useState(false);
  const [flash, setFlash] = useState("");
  const towersRef = useRef<Tower[]>([]);
  const enemiesRef = useRef<Enemy[]>([]);
  const bulletsRef = useRef<{ x: number; y: number; dx: number; dy: number }[]>([]);
  const hoverRef = useRef<{ x: number; y: number } | null>(null);
  const animRef = useRef<number>(0);
  const spawnRef = useRef(0);
  const flashTimer = useRef<number>(0);

  const showFlash = useCallback((msg: string) => {
    setFlash(msg);
    window.clearTimeout(flashTimer.current);
    flashTimer.current = window.setTimeout(() => setFlash(""), 1500);
  }, []);

  useEffect(() => () => window.clearTimeout(flashTimer.current), []);

  const reset = useCallback(() => {
    towersRef.current = [{ x: 300, y: 180 }];
    enemiesRef.current = []; bulletsRef.current = []; spawnRef.current = 0;
    hoverRef.current = null;
    setScore(0); setLives(10); setGameOver(false); setFlash("");
  }, []);

  useEffect(() => { reset(); }, [reset]);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const W = canvas.width, H = canvas.height;

    ctx.fillStyle = "#0F0F23"; ctx.fillRect(0, 0, W, H);

    // grid
    ctx.strokeStyle = "rgba(76,29,149,0.35)"; ctx.lineWidth = 1;
    for (let x = 40; x < W; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke(); }
    for (let y = 40; y < H; y += 40) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke(); }
    ctx.strokeStyle = "#4C1D95"; ctx.strokeRect(0, 0, W, H);

    // hover placement preview
    const hover = hoverRef.current;
    if (hover && !gameOver) {
      const full = towersRef.current.length >= MAX_TOWERS;
      const tooClose = towersRef.current.some((t) => Math.hypot(t.x - hover.x, t.y - hover.y) < MIN_TOWER_GAP);
      const ok = !full && !tooClose;
      const color = ok ? "#00FF41" : "#F43F5E";
      ctx.setLineDash([4, 4]);
      ctx.strokeStyle = color; ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.arc(hover.x, hover.y, RANGE, 0, Math.PI * 2); ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = ok ? "rgba(124,58,237,0.35)" : "rgba(244,63,94,0.25)";
      ctx.fillRect(hover.x - 12, hover.y - 12, 24, 24);
      ctx.strokeStyle = color; ctx.strokeRect(hover.x - 12, hover.y - 12, 24, 24);
    }

    // towers
    for (const t of towersRef.current) {
      ctx.fillStyle = "#7C3AED"; ctx.fillRect(t.x - 12, t.y - 12, 24, 24);
      ctx.strokeStyle = "#A78BFA"; ctx.strokeRect(t.x - 12, t.y - 12, 24, 24);
    }

    // enemies + hp bars
    for (const e of enemiesRef.current) {
      ctx.fillStyle = "#F43F5E"; ctx.beginPath();
      ctx.arc(e.x, e.y, 12, 0, Math.PI * 2); ctx.fill();
      const ratio = Math.max(0, e.hp / e.maxHp);
      ctx.fillStyle = "rgba(15,15,35,0.9)"; ctx.fillRect(e.x - 13, e.y - 22, 26, 4);
      ctx.fillStyle = ratio > 0.5 ? "#00FF41" : "#FBBF24"; ctx.fillRect(e.x - 13, e.y - 22, 26 * ratio, 4);
    }

    // bullets
    for (const b of bulletsRef.current) {
      ctx.fillStyle = "#00FF41"; ctx.beginPath();
      ctx.arc(b.x, b.y, 4, 0, Math.PI * 2); ctx.fill();
    }
  }, [gameOver]);

  const running = isDesktop && !reduced && !gameOver && inView;

  useEffect(() => {
    if (!running) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const loop = () => {
      const W = canvas.width, H = canvas.height;
      if (spawnRef.current <= 0) {
        enemiesRef.current.push({ x: Math.random() * W, y: -20, hp: 3, maxHp: 3, speed: 1 + Math.random() * 2 });
        spawnRef.current = 60;
      }
      spawnRef.current--;

      for (const e of enemiesRef.current) e.y += e.speed;

      for (const t of towersRef.current) {
        let closest = -1, minDist = Infinity;
        for (let i = 0; i < enemiesRef.current.length; i++) {
          const dist = Math.hypot(t.x - enemiesRef.current[i].x, t.y - enemiesRef.current[i].y);
          if (dist < minDist) { minDist = dist; closest = i; }
        }
        if (closest >= 0 && minDist < RANGE) {
          const e = enemiesRef.current[closest];
          const angle = Math.atan2(e.y - t.y, e.x - t.x);
          bulletsRef.current.push({ x: t.x, y: t.y, dx: Math.cos(angle) * 5, dy: Math.sin(angle) * 5 });
        }
      }

      for (let b = bulletsRef.current.length - 1; b >= 0; b--) {
        bulletsRef.current[b].x += bulletsRef.current[b].dx;
        bulletsRef.current[b].y += bulletsRef.current[b].dy;
        if (bulletsRef.current[b].x < 0 || bulletsRef.current[b].x > W || bulletsRef.current[b].y < 0 || bulletsRef.current[b].y > H) bulletsRef.current.splice(b, 1);
      }

      for (let b = bulletsRef.current.length - 1; b >= 0; b--) {
        for (let e = enemiesRef.current.length - 1; e >= 0; e--) {
          if (Math.hypot(bulletsRef.current[b].x - enemiesRef.current[e].x, bulletsRef.current[b].y - enemiesRef.current[e].y) < 15) {
            enemiesRef.current[e].hp--; bulletsRef.current.splice(b, 1);
            if (enemiesRef.current[e].hp <= 0) { enemiesRef.current.splice(e, 1); setScore((s) => s + 100); }
            break;
          }
        }
      }

      for (let e = enemiesRef.current.length - 1; e >= 0; e--) {
        if (enemiesRef.current[e].y > H) {
          enemiesRef.current.splice(e, 1);
          setLives((l) => { if (l <= 1) { setGameOver(true); return 0; } return l - 1; });
        }
      }

      draw();
      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(animRef.current);
  }, [running, draw]);

  useEffect(() => { if (!running) draw(); }, [running, draw, score, lives]);

  const toCanvasCoords = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = canvasRef.current?.getBoundingClientRect();
    if (!rect) return null;
    return {
      x: ((e.clientX - rect.left) / rect.width) * 600,
      y: ((e.clientY - rect.top) / rect.height) * 400,
    };
  }, []);

  const handleCanvasMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    hoverRef.current = toCanvasCoords(e);
  }, [toCanvasCoords]);

  const handleCanvasLeave = useCallback(() => { hoverRef.current = null; }, []);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (gameOver) return;
    const pos = toCanvasCoords(e);
    if (!pos) return;
    if (towersRef.current.length >= MAX_TOWERS) { showFlash(`Max ${MAX_TOWERS} towers reached`); return; }
    if (towersRef.current.some((t) => Math.hypot(t.x - pos.x, t.y - pos.y) < MIN_TOWER_GAP)) { showFlash("Too close to another tower"); return; }
    towersRef.current.push(pos);
    showFlash(`Tower deployed (${towersRef.current.length}/${MAX_TOWERS})`);
  }, [gameOver, toCanvasCoords, showFlash]);

  if (!isDesktop) return (
    <div ref={sectionRef} className="scroll-mt-24">
      <GameHeader name="tower-defense" note="Place towers, defend the shell." />
      <GameDesktopCard
        tagline="Click the canvas to place towers and stop the rogue processes before they reach the prompt."
        bullets={
          <>
            <li>Click an empty spot to deploy a tower (up to 5)</li>
            <li>Towers fire at the nearest enemy inside their range</li>
          </>
        }
      />
    </div>
  );

  return (
    <div ref={sectionRef} className="scroll-mt-24">
      <GameHeader name="tower-defense" note="Place towers, defend the shell." />
      <div className="mb-4 flex justify-center gap-6 font-mono text-sm">
        <span className="text-[color:var(--green)]">Score: {score}</span>
        <span className="flex items-center gap-2 text-[#F43F5E]">
          Lives:
          <span className="inline-flex items-center gap-1" aria-label={`${lives} of 10 lives remaining`}>
            {Array.from({ length: 10 }).map((_, i) => (
              <span
                key={i}
                className="inline-block w-2.5 h-3 rounded-[1px]"
                style={{ background: i < lives ? "#F43F5E" : "#3B2A5A" }}
              />
            ))}
          </span>
        </span>
      </div>
      <div className="flex flex-col items-center">
        <canvas
          ref={canvasRef}
          width={600}
          height={400}
          className="neon-border rounded-sm cursor-crosshair"
          onClick={handleCanvasClick}
          onMouseMove={handleCanvasMove}
          onMouseLeave={handleCanvasLeave}
        />
        <div className="w-full max-w-[600px] mt-3 flex flex-wrap items-center justify-between gap-2 font-mono text-xs">
          <span className="flex items-center gap-3 text-[#A78BFA]/70">
            <span className="flex items-center gap-1">
              <span className="inline-block w-3 h-3" style={{ background: "#7C3AED", border: "1px solid #A78BFA" }} /> tower
            </span>
            <span className="flex items-center gap-1">
              <span className="inline-block w-3 h-3 rounded-full" style={{ background: "#F43F5E" }} /> enemy + HP bar
            </span>
            <span className="flex items-center gap-1">
              <span className="inline-block w-3 h-3 rounded-full" style={{ background: "#00FF41" }} /> bullet
            </span>
          </span>
          <span className="h-4 text-[#00FF41]">{flash}</span>
        </div>
        <p className="font-mono text-xs text-[color:var(--text-3)] mt-1">
          Hover to preview range ({RANGE}px) · click empty space to place a tower · lose a life when an enemy escapes.
        </p>
      </div>
      {gameOver && (
        <div className="text-center mt-4">
          <p className="font-mono text-xl text-[#F43F5E] mb-2">SYSTEM COMPROMISED</p>
          <button onClick={reset} className="px-4 py-2 font-mono text-sm text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10">[reboot]</button>
        </div>
      )}
    </div>
  );
}