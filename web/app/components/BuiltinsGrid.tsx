import GlitchCard from "./GlitchCard";

/* Mirrors the builtin table in builtins.c — keep in sync. */
const BUILTINS: { name: string; desc: string; fishZsh?: boolean }[] = [
  { name: "cd", desc: "Change directory" },
  { name: "echo", desc: "Print arguments" },
  { name: "pwd", desc: "Print working dir" },
  { name: "export", desc: "Mark for export" },
  { name: "unset", desc: "Drop a variable" },
  { name: "readonly", desc: "Freeze a variable" },
  { name: "alias", desc: "Define aliases" },
  { name: "unalias", desc: "Remove an alias" },
  { name: "source", desc: "Run a script file" },
  { name: ".", desc: "Alias of source" },
  { name: "exit", desc: "Leave the shell" },
  { name: "type", desc: "Resolve a command" },
  { name: "jobs", desc: "List background jobs" },
  { name: "fg", desc: "Bring a job to front" },
  { name: "bg", desc: "Resume a job in back" },
  { name: "wait", desc: "Wait for jobs" },
  { name: "history", desc: "Command history" },
  { name: "fc", desc: "Re-run / edit history" },
  { name: "read", desc: "Read stdin (-p prompt, -r raw, -t timeout)" },
  { name: "test", desc: "Conditional eval" },
  { name: "[", desc: "Bracket form of test" },
  { name: "true", desc: "Return 0" },
  { name: "false", desc: "Return 1" },
  { name: "exec", desc: "Replace the process" },
  { name: "shift", desc: "Shift positional params" },
  { name: "times", desc: "Shell / user CPU times" },
  { name: "trap", desc: "Signal handlers" },
  { name: "umask", desc: "File creation mask" },
  { name: "break", desc: "Exit a loop" },
  { name: "continue", desc: "Next iteration" },
  { name: "return", desc: "Return from a function" },
  { name: "set", desc: "Shell options" },
  { name: "declare", desc: "Declare a variable" },
  { name: "typeset", desc: "Alias of declare" },
  { name: "local", desc: "Function-scoped value" },
  { name: "compgen", desc: "Generate completions" },
  { name: "complete", desc: "Register completion", fishZsh: true },
  { name: "abbr", desc: "fish-style abbreviation", fishZsh: true },
  { name: "pushd", desc: "Push onto the dir stack", fishZsh: true },
  { name: "popd", desc: "Pop the dir stack", fishZsh: true },
  { name: "dirs", desc: "Show the dir stack", fishZsh: true },
  { name: "setopt", desc: "Enable a zsh option", fishZsh: true },
  { name: "unsetopt", desc: "Disable a zsh option", fishZsh: true },
  { name: "help", desc: "Builtin help" },
];

export default function BuiltinsGrid() {
  return (
    <section id="builtins" className="shell section">
      <div className="mb-12 text-center">
        <p className="kicker">/builtins</p>
        <h2 className="section-title">44 builtins</h2>
        <p className="section-sub">
          Everything from <span className="text-[color:var(--accent)]">cd</span> to job control lives in
          the shell itself — green cards are the fish &amp; zsh additions.
        </p>
      </div>

      {/* flex-wrap so a short last row stays centered instead of hanging left */}
      <div className="flex flex-wrap justify-center gap-3">
        {BUILTINS.map((cmd) => (
          <GlitchCard
            key={cmd.name}
            title={cmd.name}
            subtitle={cmd.desc}
            glowColor={cmd.fishZsh ? "#4ADE80" : "#A78BFA"}
            className="w-[calc(50%-0.375rem)] sm:w-[calc(33.333%-0.5rem)] lg:w-[9.75rem]"
          />
        ))}
      </div>
    </section>
  );
}