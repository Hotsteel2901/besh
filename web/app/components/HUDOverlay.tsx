"use client";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

/* The static scanlines live in globals.css; this overlay only adds the frame,
 * vignette and a whisper of flicker — the old version stacked a second moving
 * scanline on top, which turned the whole page into moiré. */
export default function HUDOverlay() {
  const reduced = useReducedMotion();

  return (
    <div className="crt-overlay pointer-events-none fixed inset-0 z-[9999]" aria-hidden="true">
      {/* Corner brackets */}
      <div className="absolute left-4 top-4 h-8 w-8 border-l-2 border-t-2 border-[#7C3AED]/50" />
      <div className="absolute right-4 top-4 h-8 w-8 border-r-2 border-t-2 border-[#7C3AED]/50" />
      <div className="absolute bottom-4 left-4 h-8 w-8 border-b-2 border-l-2 border-[#7C3AED]/50" />
      <div className="absolute bottom-4 right-4 h-8 w-8 border-b-2 border-r-2 border-[#7C3AED]/50" />

      {/* CRT vignette */}
      <div
        className="absolute inset-0"
        style={{
          background: "radial-gradient(ellipse at center, transparent 65%, rgba(15,15,35,0.7) 100%)",
        }}
      />

      {/* Very subtle flicker */}
      {!reduced && (
        <div
          className="absolute inset-0 opacity-[0.008] animate-crt-flicker"
          style={{ background: "rgba(124,58,237,0.4)" }}
        />
      )}
    </div>
  );
}