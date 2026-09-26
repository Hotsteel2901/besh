#!/usr/bin/env python3
"""Interactive tests for besh — things that need a real terminal.

Covers the line editor and the pieces of history/completion that only
exist at the prompt (script files do not accumulate history, exactly as
in bash):

  * UTF-8 aware backspace  (one Backspace deletes a whole character)
  * history -N / history -d
  * fc -l / fc -s / fc -s old=new
  * Tab completion via `complete -W` and `complete -F` (both the
    stdout-printing and the COMPREPLY-setting flavours)
  * command_not_found_handler defined in ~/.beshrc

Run: tests/interactive.py [-v]
Exit status is non-zero when any check fails.
"""
import os
import pty
import re
import select
import shutil
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BESH = os.environ.get("BESH", os.path.join(ROOT, "besh"))

VERBOSE = "-v" in sys.argv
PASS = 0
FAIL = 0


def strip_ansi(s):
    """Drop colour/CSI sequences and cursor moves so we can match text."""
    s = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s)
    s = s.replace("\r", "\n")
    return s


def run_session(keys, rc_lines=(), timeout=0.6, settle=0.30):
    """Fork besh on a pty, feed `keys` one at a time, return all output."""
    home = tempfile.mkdtemp(prefix="besh_test_")
    if rc_lines:
        with open(os.path.join(home, ".beshrc"), "w") as fh:
            fh.write("\n".join(rc_lines) + "\n")

    pid, fd = pty.fork()
    if pid == 0:                                # child
        os.environ["PS1"] = "besh$ "
        os.environ["TERM"] = "xterm"
        os.environ["HOME"] = home
        os.chdir(home)
        os.execv(BESH, [BESH, "-i"])

    out = b""
    time.sleep(timeout)
    for k in keys:
        os.write(fd, k)
        time.sleep(settle)
        try:
            while select.select([fd], [], [], settle)[0]:
                d = os.read(fd, 8192)
                if not d:
                    break
                out += d
        except OSError:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
    except Exception:
        pass
    shutil.rmtree(home, ignore_errors=True)
    return strip_ansi(out.decode("utf-8", "replace"))


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("ok    %s" % name)
    else:
        FAIL += 1
        print("FAIL  %s" % name)
        if VERBOSE and detail:
            print("      %s" % detail.replace("\n", "\n      "))


# --------------------------------------------------------------------
# 1. UTF-8 backspace deletes a whole character
# --------------------------------------------------------------------
o = run_session(["echo ".encode(), "测".encode(), "试".encode(),
                 b"\x7f", b"\x7f", b"OK", b"\r"])
check("utf8: two backspaces remove both CJK chars",
      "echo OK" in o and "测试" not in o.split("\n")[-1],
      repr(o[-200:]))
check("utf8: command still runs correctly", "\nOK\n" in o, repr(o[-200:]))

# --------------------------------------------------------------------
# 2. history -N and history -d
# --------------------------------------------------------------------
o = run_session([b"echo a1\r", b"echo a2\r", b"echo a3\r", b"echo a4\r",
                 b"history -3\r"])
check("history -N lists only the last N",
      "echo a4" in o and "echo a2" not in o.split("history -3")[0][-80:],
      repr(o[-400:]))
check("history -N does not crash", "double free" not in o and "Segmentation" not in o,
      repr(o[-200:]))

o = run_session([b"echo b1\r", b"echo b2\r", b"echo b3\r",
                 b"history -d 2\r", b"history\r"])
check("history -d removes the right entry",
      "echo b1" in o and "echo b3" in o and "echo b2" not in o.split("history -d 2")[-1],
      repr(o[-500:]))
check("history -d leaves no double free", "double free" not in o, repr(o[-200:]))

# --------------------------------------------------------------------
# 3. fc
# --------------------------------------------------------------------
o = run_session([b"echo AAA\r", b"echo BBB\r", b"fc -l\r"])
check("fc -l lists history with line numbers",
      re.search(r"\b1\s+echo AAA", o) and re.search(r"\b2\s+echo BBB", o),
      repr(o[-400:]))

o = run_session([b"echo ZZZ\r", b"fc -s\r"])
check("fc -s re-runs the previous command",
      o.count("ZZZ") >= 3 and "fc -s\r\nfc -s" not in o.replace(" ", ""),
      repr(o[-400:]))

o = run_session([b"echo hello123\r", b"fc -s hello=world\r"])
check("fc -s old=new substitutes before running",
      "echo world123" in o and "\nworld123\n" in o, repr(o[-300:]))

o = run_session([b"echo PREFIXED\r", b"fc -s echo\r"])
check("fc -s PREFIX finds by prefix",
      "echo PREFIXED" in o and "\nPREFIXED\n" in o, repr(o[-300:]))

# editor path: a script that rewrites the temp file
ed = os.path.join(tempfile.gettempdir(), "besh_test_ed.sh")
with open(ed, "w") as fh:
    fh.write('#!/bin/sh\nsed -i "s/AAA/EDITED/" "$1"\n')
os.chmod(ed, 0o755)
o = run_session([b"echo AAA\r", ("fc -e %s -1\r" % ed).encode()])
check("fc -e edits the selection then runs it",
      "EDITED" in o and "AAA" not in o.split("fc -e")[-1], repr(o[-300:]))
os.unlink(ed)

# --------------------------------------------------------------------
# 4. Tab completion
# --------------------------------------------------------------------
o = run_session([b"mycmd al\t", b"\r"],
                rc_lines=['complete -W "alpha beta gamma" mycmd'])
check("complete -W drives Tab", "alpha" in o, repr(o[-300:]))

o = run_session([b"stdoutcmd on\t", b"\r"],
                rc_lines=['_sf() { printf "onepct\\ntwopct\\n"; }',
                          'complete -F _sf stdoutcmd'])
check("complete -F reads candidates from stdout", "onepct" in o, repr(o[-300:]))

o = run_session([b"creplycmd cr\t", b"\r"],
                rc_lines=['_cf() { COMPREPLY="crcand1 crcand2"; }',
                          'complete -F _cf creplycmd'])
check("complete -F reads COMPREPLY", "crcand" in o, repr(o[-300:]))

o = run_session([b"compgen -A variable HO\r"])
check("compgen -A variable works at the prompt",
      "HOSTNAME" in o or "HOME" in o, repr(o[-300:]))

# --------------------------------------------------------------------
# 5. command_not_found_handler from ~/.beshrc
# --------------------------------------------------------------------
o = run_session([b"nosuchthing_abc\r", b"echo done\r"],
                rc_lines=['command_not_found_handler() {'
                          ' echo "CNF:$1" >&2; return 127; }'])
check("cnf handler defined in rc is invoked",
      "CNF:nosuchthing_abc" in o, repr(o[-300:]))
check("shell keeps running after a handled miss", "\ndone\n" in o, repr(o[-300:]))

print()
print("interactive: PASS=%d FAIL=%d" % (PASS, FAIL))
sys.exit(1 if FAIL else 0)
