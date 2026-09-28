"use client";
import { useEffect, useState } from "react";
import { useInViewport } from "@/app/hooks/useInViewport";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface Stat { label: string; value: number; suffix: string; }

const STATS: Stat[] = [
  { label: "Lines of C", value: 13198, suffix: "" },
  { label: "Builtins", value: 44, suffix: "" },
  { label: "Tokens/sec", value: 98420, suffix: "" },
  { label: "Parse speed", value: 1.2, suffix: "ms" },
  { label: "Binary size", value: 805, suffix: "KB" },
  { label: "Dependencies", value: 0, suffix: "" },
];

function AnimatedCounter({ value, suffix, animate }: { value: number; suffix: string; animate: boolean }) {
  const decimals = Number.isInteger(value) ? 0 : 1;
  const [display, setDisplay] = useState(animate ? 0 : value);

  useEffect(() => {
    if (!animate) {
      setDisplay(value);
      return;
    }
    let raf = 0;
    const start = performance.now();
    const duration = 1200;
    const tick = (now: number) => {
      const t = Math.min(1, (now - start) / duration);
      setDisplay(value * (1 - Math.pow(1 - t, 3)));
      if (t < 1) raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [value, animate]);

  return (
    <span>
      {display.toLocaleString(undefined, {
        minimumFractionDigits: decimals,
        maximumFractionDigits: decimals,
      })}
      {suffix}
    </span>
  );
}

export default function BenchmarkBar() {
  const reduced = useReducedMotion();
  const { ref, inView } = useInViewport<HTMLElement>(0.3);
  const [started, setStarted] = useState(false);

  useEffect(() => {
    if (inView) setStarted(true);
  }, [inView]);

  const animate = started && !reduced;

  return (
    <section id="benchmark" ref={ref} className="shell section">
      <div className="mb-12 text-center">
        <p className="kicker">/benchmark</p>
        <h2 className="section-title">Spec sheet</h2>
        <p className="section-sub">
          Line count, builtin count and binary size are measured from the current build;
          throughput and parse speed are rough estimates. Because every shell needs a spec sheet.
        </p>
      </div>

      <div className="grid grid-cols-2 gap-3 md:grid-cols-3 lg:grid-cols-6">
        {STATS.map((stat) => (
          <div key={stat.label} className="panel p-4 text-center">
            <div className="font-display text-2xl font-black text-[color:var(--green)] md:text-[26px]">
              <AnimatedCounter value={stat.value} suffix={stat.suffix} animate={animate} />
            </div>
            <div className="mt-1.5 font-mono text-[11px] uppercase tracking-wider text-[color:var(--text-3)]">
              {stat.label}
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}