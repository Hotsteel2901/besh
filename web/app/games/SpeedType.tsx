"use client";
import { useState, useEffect, useCallback, useRef } from "react";
import { useInViewport } from "@/app/hooks/useInViewport";
import GameHeader from "@/app/components/GameHeader";

const WORDS = "echo cd pwd ls cat grep sed awk export unset alias source exit jobs fg bg history set read test true false exec shift times trap umask break continue return help type".split(" ");
const GAME_SECONDS = 30;
const BEST_KEY = "besh.speedtype.best";

type Phase = "idle" | "countdown" | "playing" | "paused" | "over";

export default function SpeedType() {
  const { ref: sectionRef, inView } = useInViewport<HTMLElement>();
  const [phase, setPhase] = useState<Phase>("idle");
  const [countdown, setCountdown] = useState(3);
  const [current, setCurrent] = useState("");
  const [target, setTarget] = useState("");
  const [score, setScore] = useState(0);
  const [time, setTime] = useState(GAME_SECONDS);
  const [best, setBest] = useState(0);
  const [newBest, setNewBest] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);
  const bestRef = useRef(0);

  const newWord = useCallback(() => {
    setCurrent(WORDS[Math.floor(Math.random() * WORDS.length)]);
  }, []);

  // Load the persisted high score (best-effort).
  useEffect(() => {
    try {
      const v = window.localStorage.getItem(BEST_KEY);
      const n = v ? Number(v) : 0;
      if (n > 0) { bestRef.current = n; setBest(n); }
    } catch { /* localStorage unavailable */ }
  }, []);

  const start = useCallback(() => {
    setScore(0); setTarget(""); setTime(GAME_SECONDS); setCountdown(3); setNewBest(false);
    newWord();
    setPhase("countdown");
  }, [newWord]);

  // Pre-start countdown.
  useEffect(() => {
    if (phase !== "countdown") return;
    if (countdown <= 0) { setPhase("playing"); return; }
    const t = window.setTimeout(() => setCountdown((c) => c - 1), 800);
    return () => window.clearTimeout(t);
  }, [phase, countdown]);

  // Keep the input focused while the round is live.
  useEffect(() => {
    if (phase === "playing") inputRef.current?.focus();
  }, [phase]);

  // Match clock.
  useEffect(() => {
    if (phase !== "playing") return;
    const t = window.setInterval(() => setTime((prev) => (prev <= 1 ? 0 : prev - 1)), 1000);
    return () => window.clearInterval(t);
  }, [phase]);

  useEffect(() => {
    if (phase === "playing" && time <= 0) setPhase("over");
  }, [phase, time]);

  // Record the high score once a round ends.
  useEffect(() => {
    if (phase !== "over") return;
    const isNew = score > bestRef.current;
    setNewBest(isNew);
    if (isNew) {
      bestRef.current = score;
      setBest(score);
      try { window.localStorage.setItem(BEST_KEY, String(score)); } catch { /* ignore */ }
    }
  }, [phase, score]);

  // Pause whenever the section scrolls out of view.
  useEffect(() => {
    if (phase === "playing" && !inView) setPhase("paused");
  }, [phase, inView]);

  const resume = useCallback(() => setPhase("playing"), []);

  const handleChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const val = e.target.value;
    if (val === current) { setScore((s) => s + 1); setTarget(""); newWord(); return; }
    setTarget(val);
  }, [current, newWord]);

  // Losing focus stops the clock instead of silently burning time.
  const handleBlur = useCallback(() => {
    setPhase((p) => (p === "playing" ? "paused" : p));
  }, []);

  const statusHint =
    phase === "playing" ? "Input focused — click away or Tab out to pause"
      : phase === "paused" ? "Paused — the clock is stopped"
        : "Match each command exactly · +1 per command";

  return (
    <div ref={sectionRef} className="scroll-mt-24">
      <GameHeader name="speed-type" note="Type the commands as fast as you can." />
      <div className="neon-border rounded-sm w-full max-w-lg mx-auto p-8 bg-[#0F0F23]/80">
        <div className="min-h-[17rem] flex flex-col items-center justify-center text-center">
          {phase === "idle" && (
            <>
              <p className="font-mono text-2xl text-[#00FF41] neon-glow-green mb-3">ready?</p>
              <p className="font-mono text-xs text-[#A78BFA]/60 mb-6 max-w-xs">
                You get {GAME_SECONDS} seconds. Type the command shown, exactly, and it counts instantly.
              </p>
              <button onClick={start} className="px-6 py-3 font-mono text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">
                [start]
              </button>
              {best > 0 && <p className="font-mono text-xs text-[#FBBF24] mt-4">High score: {best}</p>}
            </>
          )}

          {phase === "countdown" && (
            <>
              <p className="font-mono text-sm text-[#A78BFA] mb-3">Get ready…</p>
              <p className="font-display text-6xl font-black text-[#FBBF24]" style={{ textShadow: "0 0 12px #7C3AED" }}>
                {countdown > 0 ? countdown : "GO"}
              </p>
            </>
          )}

          {phase === "playing" && (
            <div className="w-full">
              <div className="flex justify-between font-mono text-sm mb-6">
                <span className="text-[#A78BFA]">Score: {score}</span>
                <span className="text-[#F43F5E]">Time: {time}s</span>
              </div>
              <div className="font-mono text-3xl text-[#00FF41] mb-6 neon-glow-green tracking-wider">{current}</div>
              <input
                ref={inputRef}
                value={target}
                onChange={handleChange}
                onBlur={handleBlur}
                className="w-full px-4 py-2 bg-transparent border border-[#4C1D95] rounded text-[#A78BFA] font-mono text-center text-lg outline-none focus:border-[#7C3AED] transition-colors"
                autoFocus
                spellCheck={false}
                autoComplete="off"
              />
              <p className="font-mono text-xs text-[#A78BFA]/40 mt-3">focus stays on the input while playing</p>
            </div>
          )}

          {phase === "paused" && (
            <>
              <p className="font-mono text-lg text-[#FBBF24] mb-2">Paused</p>
              <p className="font-mono text-xs text-[#A78BFA]/60 mb-6 max-w-xs">
                The input lost focus, so the clock stopped. Resume when you are ready.
              </p>
              <button onClick={resume} className="px-6 py-3 font-mono text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">
                [resume]
              </button>
              <p className="font-mono text-xs text-[#00FF41] mt-4">Score: {score} · {time}s left</p>
            </>
          )}

          {phase === "over" && (
            <>
              <p className="font-mono text-xl text-[#F43F5E] mb-2">Time&apos;s up!</p>
              <p className="font-display text-4xl font-black text-[#00FF41] neon-glow-green mb-2">{score}</p>
              <p className="font-mono text-xs mb-6">
                {newBest ? <span className="text-[#FBBF24]">New high score!</span> : <span className="text-[color:var(--text-2)]">High score: {best}</span>}
              </p>
              <button onClick={start} className="px-6 py-3 font-mono text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">
                [retry]
              </button>
            </>
          )}
        </div>
        <div className="mt-6 pt-4 border-t border-[#4C1D95]/60 flex flex-wrap justify-between gap-2 font-mono text-xs text-[color:var(--text-3)]">
          <span>High score: {best}</span>
          <span>{statusHint}</span>
        </div>
      </div>
    </div>
  );
}