"use client";
import { useRef, useEffect, useState, useCallback } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface Enemy { x: number; y: number; hp: number; speed: number; }
interface Tower { x: number; y: number; }

export default function TowerDefense() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [score, setScore] = useState(0);
  const [lives, setLives] = useState(10);
  const [gameOver, setGameOver] = useState(false);
  const towersRef = useRef<Tower[]>([]);
  const enemiesRef = useRef<Enemy[]>([]);
  const bulletsRef = useRef<{ x: number; y: number; dx: number; dy: number }[]>([]);
  const animRef = useRef<number>(0);
  const spawnRef = useRef(0);

  const reset = useCallback(() => {
    towersRef.current = [{ x: 300, y: 180 }];
    enemiesRef.current = []; bulletsRef.current = []; spawnRef.current = 0;
    setScore(0); setLives(10); setGameOver(false);
  }, []);

  useEffect(() => { reset(); }, [reset]);

  useEffect(() => {
    if (!isDesktop || reduced || gameOver) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const loop = () => {
      const W = canvas.width, H = canvas.height;
      if (spawnRef.current <= 0) {
        enemiesRef.current.push({ x: Math.random() * W, y: -20, hp: 3, speed: 1 + Math.random() * 2 });
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
        if (closest >= 0 && minDist < 200) {
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

      ctx.fillStyle = "#0F0F23"; ctx.fillRect(0, 0, W, H);
      ctx.strokeStyle = "#4C1D95"; ctx.strokeRect(0, 0, W, H);
      for (const t of towersRef.current) {
        ctx.fillStyle = "#7C3AED"; ctx.fillRect(t.x - 12, t.y - 12, 24, 24);
        ctx.strokeStyle = "#A78BFA"; ctx.strokeRect(t.x - 12, t.y - 12, 24, 24);
      }
      for (const e of enemiesRef.current) {
        ctx.fillStyle = "#F43F5E"; ctx.beginPath();
        ctx.arc(e.x, e.y, 12, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = "#FFF"; ctx.font = "10px monospace"; ctx.fillText(String(e.hp), e.x - 3, e.y + 4);
      }
      for (const b of bulletsRef.current) {
        ctx.fillStyle = "#00FF41"; ctx.beginPath();
        ctx.arc(b.x, b.y, 4, 0, Math.PI * 2); ctx.fill();
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
    if (towersRef.current.length < 5) towersRef.current.push({ x, y });
  }, []);

  if (!isDesktop) return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto text-center">
      <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/tower-defense</h2>
      <p className="font-mono text-sm text-[#A78BFA]/60">Open on desktop to play: defend against rogue processes!</p>
    </section>
  );

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
