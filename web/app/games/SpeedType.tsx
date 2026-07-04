"use client";
import { useState, useEffect, useCallback, useRef } from "react";

const WORDS = "echo cd pwd ls cat grep sed awk export unset alias source exit jobs fg bg history set read test true false exec shift times trap umask break continue return help type".split(" ");

export default function SpeedType() {
  const [current, setCurrent] = useState("");
  const [target, setTarget] = useState("");
  const [score, setScore] = useState(0);
  const [time, setTime] = useState(30);
  const [playing, setPlaying] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const newWord = useCallback(() => {
    setCurrent(WORDS[Math.floor(Math.random() * WORDS.length)]);
  }, []);

  const start = useCallback(() => {
    setPlaying(true); setScore(0); setTime(30); setTarget(""); newWord();
    inputRef.current?.focus();
  }, [newWord]);

  useEffect(() => {
    if (!playing || time <= 0) return;
    const t = setInterval(() => setTime((p) => { if (p <= 1) { setPlaying(false); return 0; } return p - 1; }), 1000);
    return () => clearInterval(t);
  }, [playing, time]);

  const handleChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const val = e.target.value; setTarget(val);
    if (val === current) { setScore((s) => s + 1); setTarget(""); newWord(); }
  }, [current, newWord]);

  return (
    <section className="relative z-10 py-20 px-4 max-w-4xl mx-auto">
      <div className="text-center mb-8">
        <h2 className="font-display text-2xl md:text-4xl font-black tracking-[0.15em] uppercase text-glow mb-4">/speed-type</h2>
        <p className="font-mono text-sm text-[#A78BFA]/60 mb-2">Type the commands as fast as you can!</p>
      </div>
      <div className="neon-border rounded-sm p-8 max-w-md mx-auto text-center bg-[#0F0F23]/80">
        {!playing ? (
          <div>
            {time === 0 && <p className="font-mono text-xl text-[#F43F5E] mb-4">Time&apos;s up! Score: {score}</p>}
            <button onClick={start} className="px-6 py-3 font-mono text-[#00FF41] border border-[#00FF41] rounded hover:bg-[#00FF41]/10 transition-colors">
              {time === 0 ? "[retry]" : "[start]"}
            </button>
          </div>
        ) : (
          <div>
            <div className="flex justify-between font-mono text-sm mb-6">
              <span className="text-[#A78BFA]">Score: {score}</span>
              <span className="text-[#F43F5E]">Time: {time}s</span>
            </div>
            <div className="font-mono text-3xl text-[#00FF41] mb-4 neon-glow-green tracking-wider">{current}</div>
            <input ref={inputRef} value={target} onChange={handleChange} className="w-full px-4 py-2 bg-transparent border border-[#4C1D95] rounded text-[#A78BFA] font-mono text-center text-lg outline-none focus:border-[#7C3AED] transition-colors" autoFocus spellCheck={false} autoComplete="off" />
          </div>
        )}
      </div>
    </section>
  );
}
