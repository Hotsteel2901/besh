"use client";
import { useState, useRef, useEffect, useCallback, useMemo } from "react";
import { useTerminal } from "@/app/hooks/useTerminal";

/* Every builtin name registered in builtins.c — used to color the first word
 * green (valid) or rose (unknown), fish-style. */
const KNOWN_COMMANDS = new Set([
  "abbr", "alias", "bg", "break", "cd", "continue", "dirs", "echo", "exec",
  "exit", "export", "false", "fg", "help", "history", "jobs", "popd", "pushd",
  "pwd", "read", "readonly", "return", "set", "setopt", "shift", "source",
  "test", "times", "trap", "true", "type", "umask", "unalias", "unset",
  "unsetopt", "wait", "[", ".", "clear", "ls", "cat", "neofetch", "whoami",
  "uname", "uptime", "date", "gp",
]);

const QUICK_COMMANDS = ["help", "neofetch", "gp", "echo {1..5}", "dirs -v"];

/* Fish-style syntax highlighting for a `cmd arg...` line */
function Highlighted({ text }: { text: string }) {
  const parts = text.split(" ");
  const cmd = parts[0].toLowerCase();
  const cmdColor = KNOWN_COMMANDS.has(cmd) ? "#4ADE80" : "#FB7185";

  return (
    <>
      <span style={{ color: cmdColor }}>{parts[0]}</span>
      {parts.slice(1).map((arg, i) => {
        let color = "#E8EAF6";
        if (arg.startsWith("-")) color = "#FBBF24";
        else if (arg.startsWith("$") || arg.startsWith("$(")) color = "#22D3EE";
        else if (/^\d+$/.test(arg)) color = "#F472B6";
        else if (arg.startsWith("\"")) color = "#4ADE80";
        return (
          <span key={`${arg}-${i}`} style={{ color }}>
            {" "}
            {arg}
          </span>
        );
      })}
    </>
  );
}

export default function Terminal() {
  const { history, pushLine, clear } = useTerminal();
  const [input, setInput] = useState("");
  const inputRef = useRef<HTMLInputElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const [executed, setExecuted] = useState<string[]>([]);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [history]);

  /* fish-style autosuggestion: dim the tail of the most recent command
   * that starts with the current input */
  const suggestion = useMemo(() => {
    const trimmed = input.trim();
    if (!trimmed) return "";
    for (let i = executed.length - 1; i >= 0; i--) {
      const c = executed[i].toLowerCase();
      if (c.startsWith(trimmed.toLowerCase()) && c.length > trimmed.length) {
        return executed[i].slice(trimmed.length);
      }
    }
    return "";
  }, [input, executed]);

  const handleSubmit = useCallback(
    (e: React.FormEvent) => {
      e.preventDefault();
      if (input.trim() === "clear") {
        clear();
      } else {
        pushLine(input);
        if (input.trim()) setExecuted((prev) => [...prev, input.trim()]);
      }
      setInput("");
    },
    [input, pushLine, clear]
  );

  return (
    <section id="terminal" className="shell section">
      <div className="mb-10 text-center">
        <p className="kicker">/demo</p>
        <h2 className="section-title">Terminal</h2>
        <p className="section-sub">
          A browser mock of the real prompt — fish-style autosuggestions, syntax highlighting
          and a few commands to poke at. Press <span className="text-[color:var(--green)]">→</span> to
          accept the gray suggestion.
        </p>
      </div>

      <div className="mx-auto w-full max-w-3xl">
        <div className="neon-border overflow-hidden rounded-md">
          {/* Title bar */}
          <div className="flex items-center gap-3 border-b border-[color:var(--line)] bg-[#1a1030] px-4 py-2.5">
            <div className="flex gap-2">
              <span className="h-3 w-3 rounded-full bg-[#F43F5E]" />
              <span className="h-3 w-3 rounded-full bg-[#FBBF24]" />
              <span className="h-3 w-3 rounded-full bg-[#4ADE80]" />
            </div>
            <span className="font-mono text-xs text-[color:var(--text-2)]">
              besh — zsh-style prompt
            </span>
            <span className="ml-auto font-mono text-[11px] text-[color:var(--text-3)]">v1.1.0</span>
          </div>

          {/* Terminal output */}
          <div
            ref={scrollRef}
            className="h-72 overflow-y-auto px-4 py-3 font-mono text-[13px] leading-relaxed md:h-80"
            style={{ background: "rgba(15,15,35,0.95)" }}
          >
            {history.map((line, i) => (
              <div
                key={i}
                className={
                  line.startsWith("$ ")
                    ? ""
                    : line === ""
                      ? "h-3"
                      : "text-[#4ADE80]/85"
                }
              >
                {line.startsWith("$ ") ? (
                  <>
                    <span className="text-[color:var(--accent)]">$ </span>
                    <Highlighted text={line.slice(2)} />
                  </>
                ) : line ? (
                  line
                ) : (
                  "\u00A0"
                )}
              </div>
            ))}
          </div>

          {/* Input area — clicking anywhere on the row focuses the field */}
          <form
            onSubmit={handleSubmit}
            onClick={() => inputRef.current?.focus()}
            className="flex cursor-text items-start gap-2 border-t border-[color:var(--line)] bg-[#0f0f23] px-4 py-3"
          >
            <span className="font-mono text-sm leading-6 text-[color:var(--accent)]">$</span>
            <div className="relative min-h-[1.5rem] min-w-0 flex-1 font-mono text-sm leading-6">
              {/* Mirror layer: keeps the row tall and shows live highlighting */}
              <span aria-hidden className="pointer-events-none block whitespace-pre-wrap break-words">
                {input ? (
                  <>
                    <Highlighted text={input} />
                    {suggestion && <span className="text-[color:var(--text-3)]">{suggestion}</span>}
                  </>
                ) : (
                  <span className="text-[color:var(--text-3)]">
                    type a command… (→ accepts suggestions)
                  </span>
                )}
              </span>
              <input
                ref={inputRef}
                type="text"
                value={input}
                onChange={(e) => setInput(e.target.value)}
                aria-label="Terminal input"
                className="absolute inset-0 h-full w-full border-none bg-transparent font-mono text-sm text-transparent caret-[#00FF41] outline-none"
                spellCheck={false}
                onKeyDown={(e) => {
                  if (e.key === "ArrowRight" && suggestion) {
                    e.preventDefault();
                    setInput((prev) => prev + suggestion);
                  }
                }}
              />
            </div>
            <button
              type="button"
              onClick={() => inputRef.current?.focus()}
              className="hidden shrink-0 self-center font-mono text-[11px] text-[color:var(--text-3)] transition-colors hover:text-[color:var(--green)] sm:block"
            >
              click to type
            </button>
          </form>

          {/* Quick command chips — available on every viewport, ≥34px tall */}
          <div className="flex flex-wrap items-center gap-2 border-t border-[color:var(--line)] bg-[#0f0f23] px-4 py-3">
            <span className="font-mono text-[11px] uppercase tracking-widest text-[color:var(--text-3)]">
              try
            </span>
            {QUICK_COMMANDS.map((cmd) => (
              <button
                key={cmd}
                type="button"
                onClick={() => {
                  pushLine(cmd);
                  setExecuted((prev) => [...prev, cmd]);
                  inputRef.current?.focus();
                }}
                className="btn min-h-[34px] px-2.5 text-xs"
              >
                {cmd}
              </button>
            ))}
          </div>
        </div>
      </div>
    </section>
  );
}