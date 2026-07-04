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

  return <span>{display.toLocaleString()}{suffix}</span>;
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
