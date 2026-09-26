# besh — bash-compatible shell
# ================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -Wno-format-truncation
LDFLAGS :=
LDLIBS  :=

SRCS    := main.c lexer.c parser.c executor.c builtins.c expand.c
OBJS    := $(SRCS:.c=.o)
TARGET  := besh

.PHONY: all clean run test test-diff test-interactive

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

# --- tests -----------------------------------------------------------
# test-diff        24 cases run under both bash and besh; outputs diffed
# test-interactive interactive line-editor / history / completion checks
# test             both of the above, plus a few illustrative one-liners
test-diff: $(TARGET)
	@tests/run.sh

test-interactive: $(TARGET)
	@python3 tests/interactive.py

test: $(TARGET)
	@echo "=== Differential suite (bash vs besh) ==="
	@tests/run.sh
	@echo ""
	@echo "=== Interactive suite (line editor, history, completion) ==="
	@python3 tests/interactive.py
	@echo ""
	@echo "=== Showcase ==="
	@echo "--- process substitution ---"
	@echo 'cat <(echo from-a-psub)' | ./$(TARGET)
	@echo "--- brace group + compound redirection ---"
	@echo '{ echo one; echo two; } > /tmp/besh_demo.txt; cat /tmp/besh_demo.txt; rm -f /tmp/besh_demo.txt' | ./$(TARGET)
	@echo "--- fd duplication (redirect stdout onto stderr) ---"
	@printf 'echo to-stderr >&2\n' | ./$(TARGET) 2>&1
	@echo "--- command_not_found_handler ---"
	@printf 'command_not_found_handler() { echo "hint: run \\"help\\" first" >&2; return 127; }\nno_such_cmd_here\n' | ./$(TARGET) 2>&1 || true
	@echo "--- programmable completion ---"
	@echo 'complete -W "alpha beta" demo; complete -p; compgen -W "alpha beta" -- al' | ./$(TARGET)
	@echo ""
	@echo "=== All tests done ==="

