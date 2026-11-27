CC     ?= gcc
CFLAGS ?= -Wall -Wextra -O2

shell: shell.c
	$(CC) $(CFLAGS) -o $@ $<

test: shell
	bash tests/run_tests.sh ./shell

# rebuild with AddressSanitizer + UBSan and run the same tests
asan: shell.c
	$(CC) -Wall -Wextra -g -fsanitize=address,undefined -o shell-asan shell.c
	bash tests/run_tests.sh ./shell-asan

clean:
	rm -f shell shell-asan

.PHONY: test asan clean
