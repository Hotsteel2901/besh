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
