"use client";
import GlitchCard from "./GlitchCard";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";

const BUILTINS = [
  { name: "echo", desc: "Print to stdout" },
  { name: "cd", desc: "Change directory" },
  { name: "pwd", desc: "Print working dir" },
  { name: "ls", desc: "List directory (via exec)" },
  { name: "export", desc: "Set env variables" },
  { name: "unset", desc: "Remove variables" },
  { name: "alias", desc: "Define aliases" },
  { name: "unalias", desc: "Remove aliases" },
  { name: "source", desc: "Execute script file" },
  { name: "exit", desc: "Exit the shell" },
  { name: "jobs", desc: "List background jobs" },
  { name: "fg", desc: "Foreground job" },
  { name: "bg", desc: "Background job" },
  { name: "history", desc: "Command history" },
  { name: "read", desc: "Read from stdin" },
  { name: "test/[", desc: "Conditional eval" },
  { name: "true", desc: "Return 0" },
  { name: "false", desc: "Return 1" },
  { name: "exec", desc: "Replace process" },
  { name: "shift", desc: "Shift positional params" },
  { name: "times", desc: "Process times" },
  { name: "trap", desc: "Signal handler" },
  { name: "umask", desc: "File creation mask" },
  { name: "break", desc: "Exit loop" },
  { name: "continue", desc: "Next iteration" },
  { name: "return", desc: "Return from function" },
  { name: "set", desc: "Shell options" },
  { name: "type", desc: "Command type info" },
  { name: "help", desc: "Builtin help" },
];

const COLORS = ["#7C3AED", "#A78BFA", "#F43F5E", "#00FF41"];

export default function BuiltinsGrid() {
  const isDesktop = useIsDesktop();

  return (
    <section id="builtins" className="relative z-10 py-20 px-4 max-w-7xl mx-auto">
      <div className="text-center mb-12">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">
          /builtins
        </h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 max-w-xl mx-auto">
          29 built-in commands powering the shell experience
        </p>
      </div>

      <div className={`grid gap-3 ${isDesktop ? "grid-cols-4 md:grid-cols-6" : "grid-cols-2"}`}>
        {BUILTINS.map((cmd, i) => (
          <GlitchCard
            key={cmd.name}
            title={cmd.name}
            subtitle={cmd.desc}
            glowColor={COLORS[i % COLORS.length]}
          />
        ))}
      </div>
    </section>
  );
}
