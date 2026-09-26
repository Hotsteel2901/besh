import GlitchCard from "./GlitchCard";

const BASH_CORE = [
  { title: "Lexer + Parser", desc: "Hand-written recursive-descent parser — pipelines, lists, subshells, functions, here-docs." },
  { title: "Job Control", desc: "Background jobs with fg/bg, process groups, SIGCHLD handling and the `jobs` builtin." },
  { title: "Expansion Engine", desc: "${VAR:-default}, arithmetic $((1+1)), command substitution $(cmd), globbing, tilde." },
  { title: "Control Flow", desc: "if/elif/else/fi, for/while/until loops, case/esac, break/continue, functions with $1..$9." },
  { title: "Redirections", desc: "> >> < 2> &> << <<- fd duplication, pipes with full pipeline job tracking." },
  { title: "Line Editor", desc: "Zero-dependency readline replacement — history, tab completion, colored prompt, Ctrl-R search." },
];

const FISH_ZSH = [
  { title: "Autosuggestions", desc: "fish-style history suggestions appear in gray as you type — accept with → / Tab / Ctrl-F / End." },
  { title: "Syntax Highlighting", desc: "Commands green when valid, red when not; options, directories, variables and numbers each get a color." },
  { title: "abbr", desc: "Type `gp` + Space and it expands to `git push` — abbreviations, not aliases." },
  { title: "Ctrl-R Search", desc: "Incremental reverse search with (reverse-i-search)`pattern' matching, Ctrl-G to cancel." },
  { title: "autocd + globstar", desc: "Type a directory name to cd into it; `**/*.c` matches recursively at any depth." },
  { title: "Brace + Dir Stack", desc: "{a,b,c} and {1..10} expansion, plus pushd / popd / dirs and cd +N / cd -N." },
  { title: "setopt", desc: "zsh-style options: autocd globstar noclobber xtrace allexport histignoredups and more." },
  { title: "Prompt Escapes", desc: "%n %m %~ %# %? %g prompt escapes — with git branch and exit-code display." },
];

function Group({ title, note, items }: { title: string; note: string; items: { title: string; desc: string }[] }) {
  return (
    <div className="mb-14 last:mb-0">
      <div className="mb-5 flex items-baseline gap-4 border-b border-[color:var(--line)] pb-3">
        <h3 className="font-display text-base font-bold uppercase tracking-[0.2em] text-[color:var(--accent)] md:text-lg">
          <span className="mr-1 text-[color:var(--green)]">/</span>
          {title}
        </h3>
        <p className="ml-auto hidden font-mono text-xs text-[color:var(--text-3)] sm:block">{note}</p>
      </div>
      <div className="grid gap-3 md:grid-cols-2 lg:grid-cols-3">
        {items.map((f) => (
          <GlitchCard key={f.title} title={f.title} subtitle={f.desc} />
        ))}
      </div>
    </div>
  );
}

export default function FeatureSection() {
  return (
    <section id="features" className="shell section">
      <div className="mb-12 text-center">
        <p className="kicker">/features</p>
        <h2 className="section-title">What&apos;s inside</h2>
        <p className="section-sub">
          Fourteen features borrowed from bash, fish and zsh — implemented from scratch in C,
          with no libreadline and no ncurses.
        </p>
      </div>

      <Group title="bash core" note="Everyday compatibility with the shell you already know" items={BASH_CORE} />
      <Group title="fish + zsh" note="Modern ergonomics, remixed on top" items={FISH_ZSH} />
    </section>
  );
}