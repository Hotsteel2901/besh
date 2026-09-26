"use client";
import { useEffect, useRef, useState } from "react";
import { gsap } from "gsap";
import MatrixRain from "./MatrixRain";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

const REPO = "https://github.com/Hotsteel2901/besh";
const CLONE_CMD = "git clone https://github.com/Hotsteel2901/besh && cd besh && make && ./besh";

const TAGS = ["C99", "zero dependencies", "POSIX", "bash-compatible", "fish", "zsh", "autosuggest", "globstar"];

export default function Hero() {
  const taglineRef = useRef<HTMLParagraphElement>(null);
  const ctaRef = useRef<HTMLDivElement>(null);
  const [copied, setCopied] = useState(false);
  const reduced = useReducedMotion();

  useEffect(() => {
    if (reduced) return;
    const ctx = gsap.context(() => {
      gsap.from(taglineRef.current, { opacity: 0, y: 14, delay: 0.15, duration: 0.7 });
      gsap.from(ctaRef.current, { opacity: 0, y: 10, delay: 0.3, duration: 0.6 });
    });
    return () => ctx.revert();
  }, [reduced]);

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(CLONE_CMD);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    } catch {
      setCopied(false);
    }
  };

  return (
    <section className="relative flex min-h-[88vh] flex-col items-center justify-center overflow-hidden px-5 py-24">
      <div className="pointer-events-none absolute inset-0 z-0">
        <MatrixRain />
      </div>

      <div className="relative z-10 flex w-full max-w-3xl flex-col items-center text-center">
        <span className="chip mb-7 tracking-[0.2em]">v1.1.0 · 6.1k lines of C · zero dependencies</span>

        <h1 className="font-display text-6xl font-black uppercase leading-none tracking-[0.08em] sm:text-7xl md:text-8xl">
          <span className="bg-gradient-to-r from-[#e8eaf6] via-[#c4b5fd] to-[#4ade80] bg-clip-text text-transparent">
            besh
          </span>
        </h1>

        <p
          ref={taglineRef}
          className="mt-6 max-w-xl font-mono text-sm leading-relaxed text-[color:var(--text-2)] sm:text-base"
        >
          <span className="text-[color:var(--green)]">$ </span>
          a bash-compatible shell written in C, remixed with fish &amp; zsh
          <span className="ml-1 inline-block h-4 w-2 translate-y-[3px] animate-pulse bg-[color:var(--green)]/70" />
        </p>

        <div className="mt-8 flex flex-wrap justify-center gap-2">
          {TAGS.map((tag) => (
            <span
              key={tag}
              className={`chip ${tag === "fish" || tag === "zsh" ? "!text-[color:var(--green)]" : ""}`}
            >
              {tag}
            </span>
          ))}
        </div>

        <div ref={ctaRef} className="mt-11 w-full max-w-xl">
          <div className="panel flex items-center gap-3 px-3 py-2 text-left">
            <code className="min-w-0 flex-1 truncate font-mono text-xs text-[color:var(--text-2)] sm:text-[13px]">
              <span className="text-[color:var(--green)]">$ </span>
              {CLONE_CMD}
            </code>
            <button type="button" onClick={copy} className="btn shrink-0 px-3 text-xs">
              {copied ? "copied ✓" : "copy"}
            </button>
          </div>

          <div className="mt-4 flex flex-wrap items-center justify-center gap-3">
            <a href={REPO} target="_blank" rel="noopener noreferrer" className="btn btn-primary">
              GitHub ↗
            </a>
            <a href="#terminal" className="btn">
              Try the demo ↓
            </a>
          </div>
        </div>
      </div>
    </section>
  );
}