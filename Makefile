# besh — bash-compatible shell
# ================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -Wno-format-truncation
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
	@echo "=== Test 9: Globbing (globstar) ==="
	@mkdir -p /tmp/besh_t/a/b && touch /tmp/besh_t/a/1.c /tmp/besh_t/a/b/2.c
	@echo "cd /tmp/besh_t && echo *.c && echo **/*.c" | ./$(TARGET)
	@rm -rf /tmp/besh_t
	@echo ""
	@echo "=== Test 10: Brace expansion ==="
	@echo "echo {a,b,c}.txt {1..4}" | ./$(TARGET)
	@echo ""
	@echo "=== Test 11: autocd (implicit cd) ==="
	@mkdir -p /tmp/besh_ac && echo "cd /tmp && besh_ac && pwd" | ./$(TARGET)
	@rm -rf /tmp/besh_ac
	@echo ""
	@echo "=== Test 12: dirs / pushd / popd ==="
	@echo "dirs && pushd /tmp && popd" | ./$(TARGET)
	@echo ""
	@echo "=== Test 13: Functions + shift ==="
	@printf 'f() { echo "\$$1"; shift; echo "\$$1"; }; f a b c\n' | ./$(TARGET)
	@echo ""
	@echo "=== Test 14: setopt / unsetopt ==="
	@echo "setopt autocd && setopt | grep autocd" | ./$(TARGET)
	@echo ""
	@echo "=== All tests done ==="
