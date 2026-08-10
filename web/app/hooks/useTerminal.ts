"use client";
import { useState, useCallback } from "react";

const BANNER = "besh v1.1.0 — a bash-compatible shell in C, remixed with fish & zsh";

const COMMANDS: Record<string, string[]> = {
  help: [
    "besh commands:",
    "  cd  echo  pwd  ls  cat  export  unset  alias  abbr  source  exit",
    "  jobs  fg  bg  history  set  setopt  read  test  shift  type",
    "  pushd  popd  dirs  readonly  wait  help",
    "",
    "fish/zsh features to try:",
    "  abbr -a gp \"git push\"    gp      # abbreviation expansion",
    "  echo {1..5}              echo **/*.c",
    "  pushd /var/log           dirs -v  popd",
    "  setopt                    unsetopt autocd",
    "  autocd: type a folder name to cd into it",
  ],
  whoami: ["root@cybershell"],
  pwd: ["/home/user"],
  ls: ["Makefile  builtins.c  executor.c  expand.c  lexer.c  main.c  parser.c  shell.h  besh"],
  "cat shell.h": ["/* besh - a bash-compatible shell written in C */", "#define SHELL_VERSION \"1.1.0\"", "typedef struct { int argc; char **argv; } Command;"],
  "echo hello": ["hello"],
  "echo $HOME": ["/root"],
  "echo $((1+1))": ["2"],
  "echo {1..5}": ["1 2 3 4 5"],
  "echo {a..d}": ["a b c d"],
  "echo **/*.c": ["main.c  lexer.c  parser.c  executor.c  builtins.c  expand.c"],
  date: [new Date().toString()],
  uname: ["Linux cybershell 6.1.0 x86_64"],
  uptime: ["up 42 days, 7 hours, 13 minutes"],
  setopt: [
    "setopt autocd",
    "setopt globstar",
    "setopt autosuggest",
    "setopt syntaxhighlight",
    "setopt histignoredups",
    "unsetopt noclobber",
    "unsetopt allexport",
    "unsetopt xtrace",
    "unsetopt verbose",
    "unsetopt noglob",
  ],
  "setopt autocd": [""],
  "unsetopt autocd": [""],
  "abbr -a gp \"git push\"": [""],
  gp: ["git push — expanded from abbr 'gp' ✓"],
  "abbr": ["abbr gp git push"],
  "pushd /var/log": ["/var/log /home/user"],
  pushd: ["/home/user /var/log /home/user"],
  dirs: ["/home/user /var/log"],
  "dirs -v": ["0\t/home/user", "1\t/var/log"],
  popd: ["/home/user"],
  neofetch: [
    "         -/oyddmdhs+:.                root@cybershell",
    "     -odNMMMMMMMMMNmdy+-              OS: besh 1.1.0",
    "   -yNMMMMMMMMMMMMMMMNm/             Shell: besh (cyber edition)",
    "  :mMMMMMMMMMMMMMMMNmdm+-            Terminal: /dev/tty1",
    "  .+shdmmNNNNNNNdyo/-                CPU: CyberCore i9-1337K",
    "  .-:////////::-.                    Memory: 64GB DDR6",
  ],
};

export function useTerminal() {
  const [history, setHistory] = useState<string[]>([BANNER, ""]);

  const pushLine = useCallback((input: string) => {
    setHistory((prev) => {
      const next = [...prev, `$ ${input}`];
      const cmd = input.trim().toLowerCase();
      const response = COMMANDS[cmd] || (cmd ? [`besh: command not found: ${input.trim()}`] : []);
      return [...next, ...response, ""];
    });
  }, []);

  const clear = useCallback(() => {
    setHistory([BANNER, ""]);
  }, []);

  return { history, pushLine, clear };
}
