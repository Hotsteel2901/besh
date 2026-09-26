interface GlitchCardProps {
  title: string;
  subtitle?: string;
  glowColor?: string;
  className?: string;
  children?: React.ReactNode;
}

/* Calm card: a readable title, quiet border, and a lift on hover instead of
 * the old flashing red/green glitch overlay. */
export default function GlitchCard({
  title,
  subtitle,
  glowColor = "#A78BFA",
  className = "",
  children,
}: GlitchCardProps) {
  return (
    <div
      className={`panel h-full p-4 transition-[transform,border-color] duration-200 hover:-translate-y-0.5 hover:border-[rgba(167,139,250,0.55)] ${className}`}
    >
      <h3
        className="font-display text-[13px] font-bold uppercase tracking-[0.18em]"
        style={{ color: glowColor }}
      >
        {title}
      </h3>
      {subtitle && (
        <p className="mt-1.5 font-mono text-xs leading-relaxed text-[color:var(--text-2)]">{subtitle}</p>
      )}
      {children}
    </div>
  );
}