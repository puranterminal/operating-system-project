##############################################################
# Makefile - Task 2: Malware Analysis Sandbox
# STP503CA9 - Programming and Learning Systems
# Student: Puran Rijal
##############################################################

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -pthread \
           -fstack-protector-strong \
           -D_FORTIFY_SOURCE=2

TARGETS = sandbox test_binaries/test_cpu test_binaries/test_mem \
          test_binaries/test_normal test_binaries/test_timeout

.PHONY: all clean run

all: $(TARGETS)
	@echo ""
	@echo "  Build complete."
	@echo "  Run: ./sandbox ./test_binaries/test_normal"
	@echo ""

sandbox: sandbox.c
	$(CC) $(CFLAGS) -o sandbox sandbox.c
	@echo "  [BUILD] sandbox compiled."

test_binaries/test_%: test_binaries/test_%.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "  [BUILD] $@ compiled."

clean:
	rm -f sandbox test_binaries/test_* sandbox.log
	@echo "  [CLEAN] Done."

run: all
	./sandbox ./test_binaries/test_normal
