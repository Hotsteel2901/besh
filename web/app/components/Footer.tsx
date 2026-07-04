export default function Footer() {
  return (
    <footer className="relative z-10 py-12 px-4 border-t border-[#4C1D95]">
      <div className="max-w-5xl mx-auto">
        <div className="flex flex-col md:flex-row justify-between items-center gap-4 mb-6">
          <div className="font-display text-lg font-black tracking-[0.2em] text-[#7C3AED] neon-glow">
            besh
          </div>
          <div className="flex gap-6 font-mono text-xs text-[#A78BFA]/60">
            <a href="https://github.com/Hotsteel2901/besh" target="_blank" rel="noopener noreferrer" className="hover:text-[#00FF41] transition-colors">[src]</a>
            <a href="https://github.com/Hotsteel2901/besh#readme" target="_blank" rel="noopener noreferrer" className="hover:text-[#00FF41] transition-colors">[docs]</a>
            <a href="https://github.com/Hotsteel2901/besh/blob/main/LICENSE" target="_blank" rel="noopener noreferrer" className="hover:text-[#00FF41] transition-colors">[license]</a>
            <a href="https://github.com/Hotsteel2901/besh" target="_blank" rel="noopener noreferrer" className="hover:text-[#00FF41] transition-colors">[contribute]</a>
          </div>
        </div>
        <div className="text-center font-mono text-xs text-[#4C1D95]">
          <p>besh v1.0.0 — a bash-compatible shell written in C</p>
          <p className="mt-1">Built with 3400 lines of pure C, zero dependencies, and a lot of 80&apos;s terminal nostalgia.</p>
          <p className="mt-3 text-[#00FF41]/30">████████ READY ████████</p>
        </div>
      </div>
    </footer>
  );
}
