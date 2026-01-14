ARCH ?= i386

ifeq ($(ARCH),i386)
VMA_KERNEL ?= 0xC0000000
else ifeq ($(ARCH),x86-64)
VMA_KERNEL ?= 0xFFFFFFFFC0000000
else
$(error Unsupported architecture $(ARCH))
endif
