KERNEL ?= /lib/modules/$(shell uname -r)/build

.PHONY: all kernel userspace clean

all: kernel userspace

kernel:
	$(MAKE) -C $(KERNEL) M=$(CURDIR)/kernel modules

userspace:
	$(CC) -Wall -Wextra -O2 -Iinclude/uapi -o userspace/simplefs_test userspace/simplefs_test.c

clean:
	$(MAKE) -C $(KERNEL) M=$(CURDIR)/kernel clean
	rm -f userspace/simplefs_test
