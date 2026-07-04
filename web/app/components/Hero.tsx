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
