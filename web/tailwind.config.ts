import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./app/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        "neon-purple": "#7C3AED",
        "accent-rose": "#F43F5E",
        "void-bg": "#0F0F23",
        "terminal-green": "#00FF41",
        "dark-panel": "#1A1030",
        "muted-border": "#4C1D95",
        violet: "#A78BFA",
        foreground: "#E2E8F0",
      },
      fontFamily: {
        display: ["Orbitron", "sans-serif"],
        mono: ["JetBrains Mono", "monospace"],
      },
      animation: {
        "crt-flicker": "crt-flicker 0.15s infinite alternate",
        "neon-pulse": "neon-pulse 2s ease-in-out infinite",
        "glitch": "glitch 0.3s ease-in-out",
        "scanline": "scanline 8s linear infinite",
        "typewriter": "typewriter 2s steps(20) forwards",
        "float": "float 6s ease-in-out infinite",
        "matrix-fall": "matrix-fall 1.5s linear infinite",
      },
      keyframes: {
        "crt-flicker": {
          "0%, 100%": { opacity: "1" },
          "50%": { opacity: "0.97" },
        },
        "neon-pulse": {
          "0%, 100%": { boxShadow: "0 0 5px #7C3AED, 0 0 20px #7C3AED" },
          "50%": { boxShadow: "0 0 15px #7C3AED, 0 0 40px #7C3AED, 0 0 60px #7C3AED" },
        },
        "glitch": {
          "0%, 100%": { transform: "translateX(0)" },
          "20%": { transform: "translateX(-2px)" },
          "40%": { transform: "translateX(2px)" },
          "60%": { transform: "translateX(-1px)" },
          "80%": { transform: "translateX(1px)" },
        },
        "scanline": {
          "0%": { transform: "translateY(0)" },
          "100%": { transform: "translateY(100vh)" },
        },
        "typewriter": {
          "0%": { width: "0" },
          "100%": { width: "100%" },
        },
        "float": {
          "0%, 100%": { transform: "translateY(0)" },
          "50%": { transform: "translateY(-10px)" },
        },
        "matrix-fall": {
          "0%": { transform: "translateY(-100%)", opacity: "1" },
          "100%": { transform: "translateY(100vh)", opacity: "0" },
        },
      },
    },
  },
  plugins: [],
};
export default config;
