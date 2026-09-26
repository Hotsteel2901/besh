export default function GameHeader({ name, note }: { name: string; note: string }) {
  return (
    <div className="mb-5 flex items-baseline gap-4 border-b border-[color:var(--line)] pb-3">
      <h3 className="font-display text-base font-bold uppercase tracking-[0.2em] text-[color:var(--accent)] md:text-lg">
        <span className="mr-1 text-[color:var(--green)]">/</span>
        {name}
      </h3>
      <p className="ml-auto hidden font-mono text-xs text-[color:var(--text-3)] sm:block">{note}</p>
    </div>
  );
}