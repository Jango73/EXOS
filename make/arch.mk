ARCH ?= x86-32

CONFIG_TOML_NAME ?= exos.toml
CONFIG_TOML_NAME_UPPER ?= EXOS.TOML

ifeq ($(ARCH),x86-32)
VMA_KERNEL ?= 0xC0000000
else ifeq ($(ARCH),x86-64)
VMA_KERNEL ?= 0xFFFFFFFFC0000000
else
$(error Unsupported architecture $(ARCH))
endif
