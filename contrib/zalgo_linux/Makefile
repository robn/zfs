# Example command line to manually build source
# make modules -C /home/robn/.quiz/kernel/quiz-kernel-x86-7.2.0/kbuild/7.2.0  M=/home/robn/code/zfs/contrib/zalgo_linux
# make modules_install -C /home/robn/.quiz/kernel/quiz-kernel-x86-7.2.0/kbuild/7.2.0  M=/home/robn/code/zfs/contrib/zalgo_linux INSTALL_MOD_PATH=/home/robn/code/quiz/system/work

KBUILD_EXTRA_SYMBOLS := ../../module/Module.symvers

ccflags-y := -std=gnu11 \
	-Werror -Wno-declaration-after-statement \
	-Wmissing-prototypes -Wno-format-zero-length \
	-include ../../zfs_config.h \
	-I../../include/os/linux/kernel \
	-I../../include/os/linux/spl \
	-I../../include/os/linux/zfs \
	-I../../include \
	-D_KERNEL

obj-m := zalgo_linux.o
