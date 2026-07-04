import GlitchCard from "./GlitchCard";

const FEATURES = [
  { title: "Lexer + Parser", desc: "Hand-crafted recursive-descent parser with full POSIX grammar support — pipelines, lists, subshells, functions." },
  { title: "Job Control", desc: "Background jobs with fg/bg, process groups, SIGCHLD handling, and `jobs` builtin." },
  { title: "Built-in Line Editor", desc: "Zero-dependency readline replacement — raw terminal I/O, history, tab completion, colored prompt." },
  { title: "Expansion Engine", desc: "Variable expansion with ${VAR:-default}, arithmetic $((1+1)), command substitution $(cmd), globbing, tilde." },
  { title: "Here-Documents", desc: "<< DELIM with variable expansion, quoted delimiter support, and tab-stripping variant <<- ." },
  { title: "Control Flow", desc: "if/elif/else/fi, for/while/until loops, case/esac, break/continue, function definitions." },
];

export default function FeatureSection() {
  return (
    <section id="features" className="relative z-10 py-20 px-4 max-w-6xl mx-auto">
      <div className="text-center mb-12">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /features
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          A terminal emulator that doesn&#39;t just look like one — it thinks like one
        </p>
      </div>

      <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
        {FEATURES.map((f) => (
          <GlitchCard key={f.title} title={f.title} subtitle={f.desc} />
        ))}
      </div>
    </section>
  );
}
