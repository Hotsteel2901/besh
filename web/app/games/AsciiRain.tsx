"use client";
import { useRef, useEffect, useState } from "react";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";
import { useInViewport } from "@/app/hooks/useInViewport";
import GameDesktopCard from "@/app/components/GameDesktopCard";
import GameHeader from "@/app/components/GameHeader";

interface Drop { x: number; y: number; speed: number; chars: string[]; }

const CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$#@%&*()_+-=[]{}|;:<>?,./\\";

export default function AsciiRain() {
  const isDesktop = useIsDesktop();
  const reduced = useReducedMotion();
  const { ref: sectionRef, inView } = useInViewport<HTMLElement>();
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [input, setInput] = useState("besh");
  const [density, setDensity] = useState(30);
  const animRef = useRef<number>(0);

  useEffect(() => {
    if (!isDesktop || reduced || !inView) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    let drops: Drop[] = [];
    const chars = input || CHARS;
    drops = Array.from({ length: density }, () => ({
      x: Math.random() * canvas.width, y: Math.random() * canvas.height,
      speed: 1 + Math.random() * 4,
      chars: Array.from({ length: 10 + Math.floor(Math.random() * 20) }, () => chars[Math.floor(Math.random() * chars.length)]),
    }));

    const loop = () => {
      ctx.fillStyle = "rgba(15, 15, 35, 0.1)"; ctx.fillRect(0, 0, canvas.width, canvas.height);
      for (const d of drops) {
        for (let i = 0; i < d.chars.length; i++) {
          const alpha = 1 - i / d.chars.length;
          ctx.fillStyle = i === 0 ? `rgba(167, 139, 250, ${alpha})` : `rgba(0, 255, 65, ${alpha * 0.5})`;
          ctx.font = "14px monospace"; ctx.fillText(d.chars[i], d.x, d.y - i * 16);
        }
        d.y += d.speed;
        if (d.y > canvas.height + d.chars.length * 16) { d.y = -d.chars.length * 16; d.x = Math.random() * canvas.width; }
      }
      animRef.current = requestAnimationFrame(loop);
    };
    animRef.current = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(animRef.current);
  }, [isDesktop, reduced, inView, input, density]);

  if (!isDesktop) return (
    <div ref={sectionRef} className="scroll-mt-24">
      <GameHeader name="ascii-rain" note="Customize your own matrix rain." />
      <GameDesktopCard
        tagline="Type your own characters and pick how dense the falling rain should be."
        bullets={
          <>
            <li>Whatever you type becomes the rain</li>
            <li>Density presets from Sparse to Storm</li>
          </>
        }
      />
    </div>
  );

  return (
    <div ref={sectionRef} className="scroll-mt-24">
      <GameHeader name="ascii-rain" note="Customize your own matrix rain." />
      {!reduced ? (
        <>
          <div className="flex flex-wrap justify-center gap-4 mb-6">
            <input
              value={input}
              onChange={(e) => setInput(e.target.value)}
              className="px-4 py-2 bg-[#0F0F23] border border-[#4C1D95] rounded text-[#00FF41] font-mono text-sm outline-none focus:border-[#7C3AED]"
              placeholder="characters..."
              maxLength={20}
              spellCheck={false}
              autoComplete="off"
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
          <canvas ref={canvasRef} width={600} height={350} className="w-full max-w-2xl mx-auto neon-border rounded-sm block" />
          <p className="font-mono text-xs text-[color:var(--text-3)] text-center mt-3">
            Type to set the falling characters · {density} columns active · animation pauses when scrolled offscreen.
          </p>
        </>
      ) : (
        <div className="neon-border rounded-sm p-6 max-w-2xl mx-auto bg-[#0F0F23]/80 text-center">
          <p className="font-mono text-sm text-[#FBBF24] mb-2">Matrix rain is paused.</p>
          <p className="font-mono text-xs text-[color:var(--text-2)]">Your system requests reduced motion, so the animated canvas is disabled. Your character/density settings apply as soon as motion is allowed.</p>
        </div>
      )}
    </div>
  );
}