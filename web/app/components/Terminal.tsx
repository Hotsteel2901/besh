"use client";
import { useState, useRef, useEffect, useCallback, useMemo } from "react";
import { useTerminal } from "@/app/hooks/useTerminal";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";

const KNOWN_COMMANDS = new Set([
  "echo", "cd", "pwd", "ls", "cat", "export", "unset", "alias", "unalias",
  "abbr", "source", "exit", "jobs", "fg", "bg", "wait", "history", "read",
  "test", "true", "false", "exec", "shift", "times", "trap", "umask",
  "break", "continue", "return", "set", "setopt", "unsetopt", "pushd",
  "popd", "dirs", "type", "help", "neofetch", "whoami", "uname", "uptime", "date",
]);

const DEMO_COMMANDS = [
  "abbr -a gp \"git push\"",
  "gp",
  "echo {1..5}",
  "echo **/*.c",
  "pushd /var/log",
  "dirs -v",
  "setopt",
  "neofetch",
  "help",
];

/* Fish-style syntax highlighting for a `cmd arg...` line */
function Highlighted({ text }: { text: string }) {
  const parts = text.split(" ");
  const cmd = parts[0].toLowerCase();
  const cmdColor = KNOWN_COMMANDS.has(cmd) ? "#00FF41" : "#F43F5E";

  return (
    <>
      <span style={{ color: cmdColor }}>{parts[0]}</span>
      {parts.slice(1).map((arg, i) => {
        let color = "#E2E8F0";
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
  const isDesktop = useIsDesktop();
  const [executed, setExecuted] = useState<string[]>(DEMO_COMMANDS);

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

  const quickCommands = ["help", "neofetch", "gp", "echo {1..5}", "dirs -v"];

  return (
    <div className="w-full max-w-3xl mx-auto">
      <div className="neon-border rounded-sm overflow-hidden">
        {/* Title bar */}
        <div className="flex items-center justify-between px-4 py-2 bg-[#1A1030] border-b border-[#4C1D95]">
          <span className="text-xs text-[#00FF41] font-mono tracking-wider">
            ┌─ besh@cybershell ───────
          </span>
          <div className="flex gap-2">
            <span className="w-3 h-3 rounded-full bg-[#F43F5E]" />
            <span className="w-3 h-3 rounded-full bg-[#FBBF24]" />
            <span className="w-3 h-3 rounded-full bg-[#00FF41]" />
          </div>
        </div>

        {/* Terminal output */}
        <div
          ref={scrollRef}
          className="h-64 md:h-80 overflow-y-auto px-4 py-3 font-mono text-sm leading-relaxed"
          style={{ background: "rgba(15,15,35,0.95)" }}
        >
          {history.map((line, i) => (
            <div
              key={i}
              className={
                line.startsWith("$ ")
                  ? ""
                  : line === ""
                    ? "h-2"
                    : "text-[#00FF41]/80"
              }
            >
              {line.startsWith("$ ") ? (
                <>
                  <span className="text-[#7C3AED]">$ </span>
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

        {/* Input area */}
        <form
          onSubmit={handleSubmit}
          className="flex items-center px-4 py-2 bg-[#0F0F23] border-t border-[#4C1D95]"
        >
          <span className="text-[#7C3AED] font-mono text-sm mr-2">$</span>
          <div className="flex-1 relative font-mono text-sm">
            <Highlighted text={input} />
            {suggestion && (
              <span className="text-[#6B21A8]">{suggestion}</span>
            )}
            <input
              ref={inputRef}
              type="text"
              value={input}
              onChange={(e) => setInput(e.target.value)}
              className="absolute inset-0 bg-transparent border-none outline-none text-transparent caret-[#00FF41] font-mono text-sm w-full"
              placeholder={suggestion ? "" : "type a command... (→ accepts suggestions)"}
              autoFocus
              spellCheck={false}
              onKeyDown={(e) => {
                if (e.key === "ArrowRight" && suggestion) {
                  e.preventDefault();
                  setInput((prev) => prev + suggestion);
                }
              }}
            />
          </div>
        </form>

        {/* Quick command buttons (mobile only) */}
        {!isDesktop && (
          <div className="flex flex-wrap gap-1 px-4 py-2 bg-[#0F0F23] border-t border-[#4C1D95]">
            {quickCommands.map((cmd) => (
              <button
                key={cmd}
                type="button"
                onClick={() => pushLine(cmd)}
                className="px-2 py-1 text-xs font-mono text-[#A78BFA] border border-[#4C1D95] rounded hover:bg-[#4C1D95]/20 transition-colors"
              >
                {cmd}
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
