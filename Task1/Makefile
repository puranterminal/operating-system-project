##############################################################
# Makefile - Task 1: Privilege Separated Authentication
# STP503CA9 - Programming and Learning Systems
# Student: Puran Rijal
##############################################################

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 \
           -fstack-protector-strong \
           -D_FORTIFY_SOURCE=2

.PHONY: all clean run

all: backend frontend
	@echo ""
	@echo "  Build complete."
	@echo "  Run: sudo ./frontend"
	@echo ""

backend: backend.c
	$(CC) $(CFLAGS) -o backend backend.c
	@echo "  [BUILD] backend compiled."

frontend: frontend.c
	$(CC) $(CFLAGS) -o frontend frontend.c
	@echo "  [BUILD] frontend compiled."

clean:
	rm -f frontend backend /tmp/priv_auth.sock
	@echo "  [CLEAN] Done."

run: all
	sudo ./frontend
