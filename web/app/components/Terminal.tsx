"use client";
import { useState, useRef, useEffect, useCallback } from "react";
import { useTerminal } from "@/app/hooks/useTerminal";
import { useIsDesktop } from "@/app/hooks/useIsDesktop";

export default function Terminal() {
  const { history, pushLine, clear } = useTerminal();
  const [input, setInput] = useState("");
  const inputRef = useRef<HTMLInputElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const isDesktop = useIsDesktop();

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [history]);

  const handleSubmit = useCallback(
    (e: React.FormEvent) => {
      e.preventDefault();
      if (input.trim() === "clear") {
        clear();
      } else {
        pushLine(input);
      }
      setInput("");
    },
    [input, pushLine, clear]
  );

  const quickCommands = ["help", "neofetch", "ls", "whoami", "uname", "echo hello"];

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
                  ? "text-[#7C3AED]"
                  : line === ""
                    ? "h-2"
                    : "text-[#00FF41]/80"
              }
            >
              {line || "\u00A0"}
            </div>
          ))}
        </div>

        {/* Input area */}
        <form
          onSubmit={handleSubmit}
          className="flex items-center px-4 py-2 bg-[#0F0F23] border-t border-[#4C1D95]"
        >
          <span className="text-[#7C3AED] font-mono text-sm mr-2">$</span>
          <input
            ref={inputRef}
            type="text"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            className="flex-1 bg-transparent border-none outline-none text-[#00FF41] font-mono text-sm placeholder-[#4C1D95]"
            placeholder="type a command..."
            autoFocus
            spellCheck={false}
          />
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
