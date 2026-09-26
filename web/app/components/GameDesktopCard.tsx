import type { ReactNode } from "react";

/**
 * Consistent "desktop-only" placeholder shown for the canvas games on narrow
 * viewports, so a game never collapses into a single empty line.
 */
export default function GameDesktopCard({
  tagline,
  bullets,
}: {
  tagline: string;
  bullets?: ReactNode;
}) {
  return (
    <div className="panel mx-auto max-w-md p-6 text-center">
      <div
        className="mb-3 font-mono text-xs uppercase tracking-[0.3em] text-[color:var(--text-3)]"
        aria-hidden="true"
      >
        &gt;_
      </div>
      <p className="font-mono text-sm leading-relaxed text-[color:var(--text-1)]">{tagline}</p>
      {bullets && (
        <ul className="mt-4 space-y-1.5 font-mono text-xs text-[color:var(--text-2)]">{bullets}</ul>
      )}
      <p className="mt-5 font-mono text-xs text-[color:var(--green)]">
        Needs a keyboard and a wide canvas — open this page on a desktop to play.
      </p>
    </div>
  );
}