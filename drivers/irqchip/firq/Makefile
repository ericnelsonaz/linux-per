ifneq ($(KERNELRELEASE),)
# Kbuild part of the Makefile

obj-m += firq.o
firq-y := firq_driver.o firq_firmware.o

else
# "normal" Makefile

firq_fw.o_shipped: firq_firmware.o
	objcopy -j .text $^ $@
	#$(CROSS_COMPILE)objcopy -j .text $^ $@

endif
