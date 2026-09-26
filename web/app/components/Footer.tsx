const REPO = "https://github.com/Hotsteel2901/besh";

const LINKS = [
  { label: "source", href: REPO },
  { label: "readme", href: `${REPO}#readme` },
  { label: "license", href: `${REPO}/blob/main/LICENSE` },
  { label: "issues", href: `${REPO}/issues` },
];

export default function Footer() {
  return (
    <footer className="relative z-10 border-t border-[color:var(--line)]">
      <div className="shell flex flex-col gap-8 py-12 md:flex-row md:items-start md:justify-between">
        <div>
          <div className="font-display text-xl font-black uppercase tracking-[0.2em] text-[color:var(--accent-bright)]">
            besh
          </div>
          <p className="mt-3 max-w-sm font-mono text-xs leading-relaxed text-[color:var(--text-2)]">
            A bash-compatible shell written in C, remixed with fish &amp; zsh.
            6.3k lines, 38 builtins, zero dependencies.
          </p>
        </div>

        <nav className="flex flex-wrap gap-x-1 gap-y-1">
          {LINKS.map((link) => (
            <a
              key={link.label}
              href={link.href}
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex min-h-[40px] items-center px-3 font-mono text-[13px] text-[color:var(--text-2)] transition-colors hover:text-[color:var(--green)]"
            >
              [{link.label}]
            </a>
          ))}
        </nav>
      </div>

      <div className="border-t border-[color:var(--line)]">
        <div className="shell flex flex-col items-center gap-2 py-6 text-center md:flex-row md:justify-between md:text-left">
          <p className="font-mono text-xs text-[color:var(--text-3)]">
            besh v1.1.0 · MIT licensed · built for 80s terminal nostalgia
          </p>
          <p className="font-mono text-xs tracking-[0.2em] text-[color:var(--green)]">
            ████████ READY ████████
          </p>
        </div>
      </div>
    </footer>
  );
}