NAME=pktgene
SRCFILE:=$(wildcard *.c)

obj-m := $(NAME).o
$(NAME)-objs:=pktgene_main.o pktgene_util.o

KVERS=$(shell uname -r)
CURDIR=$(shell pwd)

build: kernel_modules

kernel_modules:
	make -C /lib/modules/$(KVERS)/build M=$(CURDIR) modules
install:
	insmod ./$(NAME).ko
	-mknod /dev/$(NAME) c 507 0
uninstall:
	-rm -f /dev/$(NAME)
	-rmmod $(NAME)
clean:
	make  -C /lib/modules/$(KVERS)/build M=$(CURDIR) clean
test:
	@echo $($(NAME)-objs)
reset:
	make uninstall -C .
	make install -C .
