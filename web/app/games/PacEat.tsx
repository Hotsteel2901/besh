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

  useEffect(() => { reset(); }, [reset]);

  useEffect(() => {
    if (!isDesktop || reduced || gameOver) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const W = 400, H = 300;

    const keys: Record<string, boolean> = {};
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
        g.x += g.dx; g.y += g.dy;
        if (g.x < 15 || g.x > W - 15) g.dx *= -1;
        if (g.y < 15 || g.y > H - 15) g.dy *= -1;
      });

      foodsRef.current = foodsRef.current.filter((f) => {
        if (Math.hypot(p.x - f.x, p.y - f.y) < 18) { setScore((s) => s + 10); return false; }
        return true;
      });

      for (const g of ghostsRef.current) {
        if (Math.hypot(p.x - g.x, p.y - g.y) < 20) { setGameOver(true); return; }
      }
      if (foodsRef.current.length === 0) { setGameOver(true); return; }

      ctx.fillStyle = "#0F0F23"; ctx.fillRect(0, 0, W, H);
      ctx.strokeStyle = "#4C1D95"; ctx.strokeRect(0, 0, W, H);
      foodsRef.current.forEach((f) => {
        ctx.fillStyle = "#00FF41"; ctx.beginPath();
        ctx.arc(f.x, f.y, 6, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = "#A78BFA"; ctx.font = "8px monospace";
        ctx.fillText(f.cmd, f.x - 12, f.y - 10);
      });
      ghostsRef.current.forEach((g) => {
        ctx.fillStyle = g.color; ctx.beginPath();
        ctx.arc(g.x, g.y, 12, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = "#FFF";
        ctx.beginPath(); ctx.arc(g.x - 4, g.y - 3, 3, 0, Math.PI * 2); ctx.fill();
        ctx.beginPath(); ctx.arc(g.x + 4, g.y - 3, 3, 0, Math.PI * 2); ctx.fill();
      });
      ctx.fillStyle = "#FBBF24"; ctx.beginPath();
      const angle = p.mouth * 0.5;
      ctx.arc(p.x, p.y, 14, p.dir > 0 ? angle : Math.PI + angle, p.dir > 0 ? Math.PI * 2 - angle : Math.PI - angle);
      ctx.lineTo(p.x, p.y); ctx.fill();
      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);
    return () => { cancelAnimationFrame(animRef.current); window.removeEventListener("keydown", onKey); window.removeEventListener("keyup", offKey); };
  }, [isDesktop, reduced, gameOver]);

  if (!isDesktop) return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto text-center">
      <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/pac-eat</h2>
      <p className="font-mono text-sm text-[#A78BFA]/60">Open on desktop to play Pac-Eat: commands edition</p>
    </section>
  );

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
          <p className="font-mono text-xl text-[#F43F5E] mb-2">{foodsRef.current.length === 0 ? "YOU WIN!" : "GAME OVER"}</p>
          <button onClick={reset} className="px-4 py-2 font-mono text-sm text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">[restart]</button>
        </div>
      )}
    </section>
  );
}
