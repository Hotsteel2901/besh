# besh — bash-compatible shell
# ================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g
LDFLAGS :=
LDLIBS  :=

SRCS    := main.c lexer.c parser.c executor.c builtins.c expand.c
OBJS    := $(SRCS:.c=.o)
TARGET  := besh

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@echo "=== Test 1: Simple command ==="
	@echo "echo hello world" | ./$(TARGET)
	@echo ""
	@echo "=== Test 2: Pipeline ==="
	@echo "echo hello world | cat" | ./$(TARGET)
	@echo ""
	@echo "=== Test 3: Builtin cd / pwd ==="
	@echo "cd /tmp && pwd" | ./$(TARGET)
	@echo ""
	@echo "=== Test 4: Variable expansion ==="
	@echo "echo \$$HOME" | ./$(TARGET)
	@echo ""
	@echo "=== Test 5: Redirection ==="
	@echo "echo test > /tmp/besh_test.txt && cat /tmp/besh_test.txt && rm /tmp/besh_test.txt" | ./$(TARGET)
	@echo ""
	@echo "=== Test 6: AND/OR ==="
	@echo "true && echo yes || echo no" | ./$(TARGET)
	@echo "false && echo yes || echo no" | ./$(TARGET)
	@echo ""
	@echo "=== Test 7: Background ==="
	@echo "sleep 0.1 &" | ./$(TARGET)
	@echo ""
	@echo "=== Test 8: History ==="
	@printf "echo first\necho second\nhistory\n" | ./$(TARGET)
	@echo ""
	@echo "=== All tests done ==="
