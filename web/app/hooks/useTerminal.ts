"use client";
import { useState, useCallback } from "react";

const COMMANDS: Record<string, string[]> = {
  help: ["besh commands:", "  echo cd pwd ls cat export unset alias source exit", "  jobs fg bg history set read test true false exec", "  shift times trap umask break continue return help"],
  whoami: ["root@cybershell"],
  pwd: ["/home/user"],
  ls: ["Makefile  builtins.c  executor.c  expand.c  lexer.c  main.c  parser.c  shell.h  besh"],
  "cat shell.h": ["/* besh - a bash-compatible shell written in C */", "#define SHELL_VERSION \"1.0.0\"", "typedef struct { int argc; char **argv; } Command;"],
  "echo hello": ["hello"],
  "echo $HOME": ["/root"],
  "echo $((1+1))": ["2"],
  date: [new Date().toString()],
  uname: ["Linux cybershell 6.1.0 x86_64"],
  uptime: ["up 42 days, 7 hours, 13 minutes"],
  neofetch: [
    "         -/oyddmdhs+:.                root@cybershell",
    "     -odNMMMMMMMMMNmdy+-              OS: besh 1.0.0",
    "   -yNMMMMMMMMMMMMMMMNm/             Shell: besh (cyber edition)",
    "  :mMMMMMMMMMMMMMMMNmdm+-            Terminal: /dev/tty1",
    "  .+shdmmNNNNNNNdyo/-                CPU: CyberCore i9-1337K",
    "  .-:////////::-.                    Memory: 64GB DDR6",
  ],
};

export function useTerminal() {
  const [history, setHistory] = useState<string[]>(["besh v1.0.0 — Type 'help' for commands", ""]);

  const pushLine = useCallback((input: string) => {
    setHistory((prev) => {
      const next = [...prev, `$ ${input}`];
      const cmd = input.trim().toLowerCase();
      const response = COMMANDS[cmd] || (cmd ? [`besh: command not found: ${input.trim()}`] : []);
      return [...next, ...response, ""];
    });
  }, []);

  const clear = useCallback(() => {
    setHistory(["besh v1.0.0 — Type 'help' for commands", ""]);
  }, []);

  return { history, pushLine, clear };
}
