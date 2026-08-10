import GlitchCard from "./GlitchCard";

const BASH_CORE = [
  { title: "Lexer + Parser", desc: "Hand-crafted recursive-descent parser — pipelines, lists, subshells, functions, here-docs." },
  { title: "Job Control", desc: "Background jobs with fg/bg, process groups, SIGCHLD handling, and the `jobs` builtin." },
  { title: "Expansion Engine", desc: "${VAR:-default}, arithmetic $((1+1)), command substitution $(cmd), globbing, tilde." },
  { title: "Control Flow", desc: "if/elif/else/fi, for/while/until loops, case/esac, break/continue, functions with $1..$9." },
  { title: "Redirections", desc: "> >> < 2> &> << <<- fd duplication, pipes with full pipeline job tracking." },
  { title: "Built-in Line Editor", desc: "Zero-dependency readline replacement — history, tab completion, colored prompt, Ctrl-R search." },
];

const FISH_ZSH = [
  { title: "Autosuggestions (fish)", desc: "History suggestions appear in gray as you type — accept with → / Tab / Ctrl-F / End." },
  { title: "Syntax Highlighting (fish)", desc: "Commands green when valid, red when not; options, directories, variables and numbers each get a color." },
  { title: "abbr (fish)", desc: "Type `gp` + Space and it expands to `git push` — abbreviations, not aliases." },
  { title: "Ctrl-R Search (zsh)", desc: "Incremental reverse search with (reverse-i-search)`pattern' matching, Ctrl-G to cancel." },
  { title: "autocd + globstar (zsh)", desc: "Type a directory name to cd into it; `**/*.c` matches recursively at any depth." },
  { title: "Brace + Dir Stack (zsh)", desc: "{a,b,c} and {1..10} expansion, plus pushd / popd / dirs and cd +N / cd -N." },
  { title: "setopt (zsh)", desc: "zsh-style options: autocd globstar noclobber xtrace allexport histignoredups and more." },
  { title: "Prompt Escapes (zsh)", desc: "%n %m %~ %# %? %g prompt escapes — with git branch and exit-code display." },
];

function Group({ title, note, items }: { title: string; note: string; items: { title: string; desc: string }[] }) {
  return (
    <div className="mb-14">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-2">
          {title}
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">{note}</p>
      </div>
      <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
        {items.map((f) => (
          <GlitchCard key={f.title} title={f.title} subtitle={f.desc} />
        ))}
      </div>
    </div>
  );
}

export default function FeatureSection() {
  return (
    <section id="features" className="relative z-10 py-20 px-4 max-w-6xl mx-auto">
      <Group title="/core" note="Everyday bash compatibility" items={BASH_CORE} />
      <Group title="/fish + zsh" note="Modern ergonomics, remixed" items={FISH_ZSH} />
    </section>
  );
}
