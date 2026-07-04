"use client";
import { useReducedMotion } from "@/app/hooks/useReducedMotion";

interface GlitchCardProps {
  title: string;
  subtitle?: string;
  glowColor?: string;
  children?: React.ReactNode;
}

export default function GlitchCard({ title, subtitle, glowColor = "#7C3AED", children }: GlitchCardProps) {
  const reduced = useReducedMotion();

  return (
    <div
      className="relative group p-4 rounded-sm border transition-all duration-300 cursor-default"
      style={{
        borderColor: glowColor,
        background: "linear-gradient(135deg, rgba(26,16,48,0.9), rgba(15,15,35,0.95))",
        boxShadow: `0 0 10px ${glowColor}22, 0 0 30px ${glowColor}11`,
      }}
      onMouseEnter={(e) => {
        if (reduced) return;
        (e.currentTarget as HTMLElement).style.boxShadow = `0 0 20px ${glowColor}44, 0 0 60px ${glowColor}22`;
        (e.currentTarget as HTMLElement).style.transform = "translateY(-2px) scale(1.02)";
      }}
      onMouseLeave={(e) => {
        (e.currentTarget as HTMLElement).style.boxShadow = `0 0 10px ${glowColor}22, 0 0 30px ${glowColor}11`;
        (e.currentTarget as HTMLElement).style.transform = "translateY(0) scale(1)";
      }}
    >
      {!reduced && (
        <div className="absolute inset-0 opacity-0 group-hover:opacity-100 transition-opacity duration-75 overflow-hidden pointer-events-none rounded-sm">
          <div className="absolute inset-0 bg-[#F43F5E]/10 animate-glitch" />
          <div className="absolute inset-0 bg-[#00FF41]/10 animate-glitch" style={{ animationDelay: "0.05s" }} />
        </div>
      )}

      <h3
        className="font-display text-sm tracking-widest uppercase mb-1"
        style={{ color: glowColor, textShadow: `0 0 5px ${glowColor}` }}
      >
        {title}
      </h3>
      {subtitle && (
        <p className="text-xs text-[#E2E8F0]/70 font-mono">{subtitle}</p>
      )}
      {children}
    </div>
  );
}
