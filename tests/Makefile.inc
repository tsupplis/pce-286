# tests/Makefile.inc
#
# C component, CPU guest and native AT regression tests.
# See README.md or `make check-help` for suites and external prerequisites.
# Native tests never require the OS/2 or daily XT sample installations.
#

rel := tests

TEST_EMULATOR ?= src/arch/ibmpc/pce-ibmpc

DIRS += $(rel)
DIST += $(rel)/Makefile.inc $(rel)/test_ibmpc_memory.c $(rel)/test_ibmpc_exit.py $(rel)/test_at_kbd.c

# -----------------------------------------------------------------------
# The e8086 archive (all CPU object files linked into one archive).
E8086_ARC := src/cpu/e8086/e8086.a src/cpu/e8080/e8080.a

# -----------------------------------------------------------------------
# Per-test binary list
TEST_BINS := \
	$(rel)/test_v20_native_audit \
	$(rel)/test_nec_8080 \
	$(rel)/test_8080_audit \
	$(rel)/test_nec_native \
	$(rel)/test_repeat_compliance \
	$(rel)/test_x86_compliance \
	$(rel)/test_disasm_286    \
	$(rel)/test_286_msw       \
	$(rel)/test_286_addr      \
	$(rel)/test_286_flags     \
	$(rel)/test_286_pm_fields \
	$(rel)/test_286_prot      \
	$(rel)/test_286_idt       \
	$(rel)/test_286_sysops    \
	$(rel)/test_286_exec      \
	$(rel)/test_286_tss       \
	$(rel)/test_286_extmem    \
	$(rel)/test_v20           \
	$(rel)/test_v20ext        \
	$(rel)/test_186ext        \
	$(rel)/test_port92        \
	$(rel)/test_at_kbd        \
	$(rel)/test_at_cmos       \
	$(rel)/test_loadall \
	$(rel)/test_at_io_audit \
	$(rel)/test_ibmpc_memory

TEST_OBJS := $(addsuffix .o, $(TEST_BINS))

# Add to clean list
CLN += $(TEST_BINS) $(TEST_OBJS)

# -----------------------------------------------------------------------
# Per-test build rules.  Each test binary links against the full e8086
# archive so it picks up every CPU object file automatically.
#
# All test .o files depend on e8086.h so make recompiles them whenever
# the CPU struct changes (prevents stale-binary false-positives).

E8086_HDR := src/cpu/e8086/e8086.h $(CPU_8080_HDR)

$(rel)/test_disasm_286.o: $(rel)/test_disasm_286.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_msw.o: $(rel)/test_286_msw.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_x86_compliance.o: $(rel)/test_x86_compliance.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_x86_compliance: $(rel)/test_x86_compliance.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

DIST += $(rel)/test_x86_compliance.c $(rel)/cpu-compliance.md

$(rel)/test_v20.o: $(rel)/test_v20.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_v20ext.o: $(rel)/test_v20ext.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_186ext.o: $(rel)/test_186ext.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_port92.o: $(rel)/test_port92.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_addr.o: $(rel)/test_286_addr.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_addr: $(rel)/test_286_addr.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_flags.o: $(rel)/test_286_flags.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_flags: $(rel)/test_286_flags.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_pm_fields.o: $(rel)/test_286_pm_fields.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_pm_fields: $(rel)/test_286_pm_fields.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_prot.o: $(rel)/test_286_prot.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_prot: $(rel)/test_286_prot.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_idt.o: $(rel)/test_286_idt.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_idt: $(rel)/test_286_idt.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_sysops.o: $(rel)/test_286_sysops.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_sysops: $(rel)/test_286_sysops.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_exec.o: $(rel)/test_286_exec.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_exec: $(rel)/test_286_exec.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_tss.o: $(rel)/test_286_tss.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_tss: $(rel)/test_286_tss.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_extmem.o: $(rel)/test_286_extmem.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_286_extmem: $(rel)/test_286_extmem.o $(E8086_ARC) \
	src/devices/memory.o
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_disasm_286: $(rel)/test_disasm_286.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_286_msw: $(rel)/test_286_msw.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_v20: $(rel)/test_v20.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_v20ext: $(rel)/test_v20ext.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_186ext: $(rel)/test_186ext.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_port92: $(rel)/test_port92.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_at_kbd.o: $(rel)/test_at_kbd.c src/arch/ibmpc/at-keyboard.h
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_at_kbd: $(rel)/test_at_kbd.o src/arch/ibmpc/at-keyboard.o
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_at_cmos.o: $(rel)/test_at_cmos.c
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_at_cmos: $(rel)/test_at_cmos.o
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

$(rel)/test_loadall.o: $(rel)/test_loadall.c $(E8086_HDR)
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

$(rel)/test_loadall: $(rel)/test_loadall.o $(E8086_ARC)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

# -----------------------------------------------------------------------
# 'make check-build' — compile all test binaries without running them.
.PHONY: check-build
check-build: $(TEST_BINS)

# -----------------------------------------------------------------------
# 'make check-unit' — build and run C unit tests only (no emulator needed).
.PHONY: check-unit
check-unit: $(TEST_BINS)
	$(QP)echo "  RUN    C unit tests"
	$(QR)result=0 ; \
	for t in $(TEST_BINS) ; do \
		printf "  TEST   $$t ... " ; \
		if $$t ; then \
			printf "OK\n" ; \
		else \
			printf "FAILED\n" ; \
			result=1 ; \
		fi ; \
	done ; \
	if test $$result -ne 0 ; then \
		echo "SOME TESTS FAILED." ; \
		exit 1 ; \
	fi ; \
	echo "All tests passed (C unit tests)."

# -----------------------------------------------------------------------
# NASM end-to-end regression tests (.asm → .com via NASM → run inside emulator)
# The NASM-based tests are only built/run when NASM is available.

ASM_DIR := $(rel)/asm

# NASM include path (matches pceutils/Makefile.inc convention)
ASM_NASM_FLAGS := -O6 -I$(srcdir)/src/ -I$(srcdir)/src/arch/ibmpc/

ASM_TEST_COMS := \
	$(ASM_DIR)/t186.com     \
	$(ASM_DIR)/t286.com     \
	$(ASM_DIR)/tv20.com     \
	$(ASM_DIR)/ta20.com     \
	$(ASM_DIR)/tprot.com    \
	$(ASM_DIR)/textmem.com  \
	$(ASM_DIR)/tloadall.com \
	$(ASM_DIR)/tataccept.com \
	$(ASM_DIR)/tfloppysoak.com \
	$(ASM_DIR)/tpmsoak.com

CLN += $(ASM_TEST_COMS)
DIST += \
	$(ASM_DIR)/t186.asm     \
	$(ASM_DIR)/t286.asm     \
	$(ASM_DIR)/tv20.asm     \
	$(ASM_DIR)/ta20.asm     \
	$(ASM_DIR)/tprot.asm    \
	$(ASM_DIR)/textmem.asm  \
	$(ASM_DIR)/tloadall.asm

ifeq "$(PCE_HAVE_NASM)" "1"
$(ASM_DIR)/%.com: $(ASM_DIR)/%.asm
	$(QP)echo "  NASM   $@"
	$(QR)$(NASM) $(ASM_NASM_FLAGS) -f bin $< -o $@

# Build target for all .com files
.PHONY: check-asm-build
check-asm-build: $(ASM_TEST_COMS)

# -----------------------------------------------------------------------
# Pre-built test floppy images (one per test, fixed path, built once).
EMU_BIN   ?= $(TEST_EMULATOR)
EMU_CFG   := tests/test.cfg
IMG_T186    := images/t186.img
IMG_T286    := images/t286.img
IMG_TV20    := images/tv20.img
IMG_TA20    := images/ta20.img
IMG_TPROT    := images/tprot.img
IMG_TEXTMEM  := images/textmem.img
IMG_TLOADALL := images/tloadall.img
CLN += $(IMG_T186) $(IMG_T286) $(IMG_TV20) $(IMG_TA20) $(IMG_TPROT) $(IMG_TEXTMEM) $(IMG_TLOADALL)

# 'make check-images' — build the seven fixed CPU test floppy images.
# Depends on the .com files and pceexit.com being up to date.
.PHONY: check-images
check-images: $(ASM_TEST_COMS) src/arch/ibmpc/pceutils/pceexit.com
	$(QP)echo "  IMAGES build test floppy images"
	$(QR)sh tests/build_images.sh

# -----------------------------------------------------------------------
# CPU guest images use null display/audio, bounded runs and retained logs.
E2E_TIMEOUT ?= 30
E2E_TEST_DIR ?= tests/memory-validation/e2e
.PHONY: check-e2e
check-e2e: $(EMU_CFG) $(EMU_BIN)
	$(QR)python3 tests/run_e2e.py --emulator "$(EMU_BIN)" --config "$(EMU_CFG)" \
		--timeout "$(E2E_TIMEOUT)" --output "$(E2E_TEST_DIR)"


else

# NASM not available — skip end-to-end tests gracefully
.PHONY: check-asm-build check-e2e
check-asm-build:
	@echo "  SKIP   NASM not available; ASM tests not built"
check-e2e:
	@echo "  SKIP   NASM not available; end-to-end tests skipped"

endif

# Normal checks include native AT smoke tests; long soaks are explicit targets.
.PHONY: check
check: check-unit check-e2e check-exit check-at-native

# Exercise the actual machine through its public constructor and I/O bus.
# Rename the application entry point so main.c supplies the existing globals;
# tests link the production objects without including implementation sources.
CLN += tests/test_at_io_audit_main.o

tests/test_at_io_audit_main.o: src/arch/ibmpc/main.c src/arch/ibmpc/main.h src/arch/ibmpc/ibmpc.h $(E8086_HDR)
	$(CC) -c $(PCE_IBMPC_CFLAGS) -Dmain=pce_program_main -o $@ $<

tests/test_at_io_audit.o: tests/test_at_io_audit.c src/arch/ibmpc/xms.h src/arch/ibmpc/main.h src/arch/ibmpc/ibmpc.h $(E8086_HDR)
	$(CC) -c $(PCE_IBMPC_CFLAGS) -o $@ $<

tests/test_at_io_audit: tests/test_at_io_audit.o tests/test_at_io_audit_main.o $(filter-out src/arch/ibmpc/main.o,$(PCE_IBMPC_OBJ)) $(PCE_IBMPC_OBJ_EXT)
	$(LD) $(PCE_IBMPC_LDFLAGS) -o $@ $^ $(PCE_IBMPC_LIBS) -lm

# Memory ownership tests use the same production machine objects.
tests/test_at_io_audit.o tests/test_at_io_audit_main.o tests/test_ibmpc_memory.o: src/arch/ibmpc/at-keyboard.h

tests/test_ibmpc_memory.o: tests/test_ibmpc_memory.c src/arch/ibmpc/ibmpc.h src/arch/ibmpc/xms.h $(E8086_HDR)
	$(CC) -c $(PCE_IBMPC_CFLAGS) -o $@ $<

tests/test_ibmpc_memory: tests/test_ibmpc_memory.o tests/test_at_io_audit_main.o $(filter-out src/arch/ibmpc/main.o,$(PCE_IBMPC_OBJ)) $(PCE_IBMPC_OBJ_EXT)
	$(LD) $(PCE_IBMPC_LDFLAGS) -o $@ $^ $(PCE_IBMPC_LIBS) -lm

# CLI cleanup regression; requires the existing Python 3 interpreter.
EXIT_TEST_BIN ?= $(TEST_EMULATOR)
.PHONY: check-exit
check-exit: $(EXIT_TEST_BIN)
	python3 tests/test_ibmpc_exit.py $(EXIT_TEST_BIN)

tests/test_at_io_audit.o tests/test_at_io_audit_main.o tests/test_ibmpc_memory.o: src/devices/wd1003.h

tests/test_at_io_audit.o tests/test_at_io_audit_main.o tests/test_ibmpc_memory.o: src/devices/fdc.h src/chipset/82xx/e8272.h

DIST += tests/test_at_native_boot.py

DIST += tests/test_at_native_acceptance.py tests/asm/tataccept.asm
DIST += tests/test_xt_daily_acceptance.py
DIST += tests/test_floppy_soak.py tests/asm/tfloppysoak.asm
DIST += tests/test_at_native_disk.py
DIST += tests/test_286_pm_soak.py tests/asm/tpmsoak.asm

# Fast model-family gate and explicit known-gap report (no ROMs required).
.PHONY: check-cpu-regression
check-cpu-regression: check-build tests/test_v20_native_audit
	$(QR)python3 tests/run_cpu_regression.py

tests/test_v20_native_audit: tests/test_v20_native_audit.c $(E8086_HDR) $(E8086_ARC)
	$(QR)$(CC) $(CFLAGS_DEFAULT) -o $@ tests/test_v20_native_audit.c $(E8086_ARC)

CLN += tests/test_v20_native_audit
DIST += tests/test_v20_native_audit.c tests/run_cpu_regression.py

tests/test_repeat_compliance.o: tests/test_repeat_compliance.c $(E8086_HDR)
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<

tests/test_repeat_compliance: tests/test_repeat_compliance.o $(E8086_ARC)
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^

DIST += tests/test_repeat_compliance.c

tests/test_nec_native.o: tests/test_nec_native.c $(E8086_HDR)
	$(QR)$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<
tests/test_nec_native: tests/test_nec_native.o $(E8086_ARC)
	$(QR)$(LD) $(LDFLAGS_DEFAULT) -o $@ $^
DIST += tests/test_nec_native.c

# 8080 regression plus shared Z80 interrupt/flag checks.
DIST += tests/test_8080_audit.c tests/8080-review.md
.PHONY: check-8080-audit
check-8080-audit: tests/test_8080_audit
	./tests/test_8080_audit

tests/test_8080_audit: tests/test_8080_audit.c $(CPU_8080_SRC) $(CPU_8080_HDR)
	$(CC) $(CFLAGS_DEFAULT) -Isrc -o $@ tests/test_8080_audit.c $(CPU_8080_SRC)
CLN += tests/test_8080_audit

tests/test_nec_8080.o: tests/test_nec_8080.c $(E8086_HDR) $(CPU_8080_HDR)
	$(CC) -c $(CFLAGS_DEFAULT) -o $@ $<
tests/test_nec_8080: tests/test_nec_8080.o $(E8086_ARC)
	$(CC) $(LDFLAGS_DEFAULT) -o $@ $^
DIST += tests/test_nec_8080.c

DIST += tests/nec-8080-mode.md

# Native AT tests use testroms/ and images/dos.img, never sample installations.
AT_TEST_DIR ?= tests/memory-validation/make-at
AT_TIMEOUT ?=
AT_SOAK_CYCLES ?= 3
AT_SOAK_ROUNDS ?= 64
AT_PM_RUNS ?= 3
AT_DISK_IMAGE ?=
AT_TIMEOUT_OPT = $(if $(strip $(AT_TIMEOUT)),--timeout "$(AT_TIMEOUT)")
AT_UNIT_BINS := tests/test_port92 tests/test_at_kbd tests/test_at_cmos \
 tests/test_at_io_audit tests/test_ibmpc_memory

.PHONY: check-at-build check-at-unit check-at check-at-native check-at-boot \
 check-at-acceptance check-at-full-ram check-at-floppy-soak check-at-disk \
 check-at-pm-soak check-all check-help

check-at-build: $(AT_UNIT_BINS) $(TEST_EMULATOR) check-asm-build

check-at-unit: $(AT_UNIT_BINS)
	$(QR)result=0; for t in $(AT_UNIT_BINS); do "$$t" || result=1; done; exit $$result

check-at: check-at-unit check-at-native
check-at-native: check-at-boot check-at-acceptance
check-at-boot: check-at-boot-ibm check-at-boot-ami
check-at-acceptance: check-at-acceptance-ibm check-at-acceptance-ami
check-at-full-ram: check-at-boot-full-ibm check-at-boot-full-ami \
 check-at-acceptance-full-ibm check-at-acceptance-full-ami
check-at-floppy-soak: check-at-floppy-ibm check-at-floppy-ami
check-all: check check-at-full-ram check-at-floppy-soak
ifneq "$(strip $(AT_DISK_IMAGE))" ""
check-all: check-at-pm-soak
endif

.PHONY: check-at-boot-ibm
check-at-boot-ibm: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite boot --bios ibm \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --output "$(AT_TEST_DIR)/boot-ibm"

.PHONY: check-at-boot-ami
check-at-boot-ami: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite boot --bios ami \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --output "$(AT_TEST_DIR)/boot-ami"

.PHONY: check-at-boot-full-ibm
check-at-boot-full-ibm: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite boot --bios ibm \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --full-ram --output "$(AT_TEST_DIR)/boot-full-ibm"

.PHONY: check-at-boot-full-ami
check-at-boot-full-ami: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite boot --bios ami \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --full-ram --output "$(AT_TEST_DIR)/boot-full-ami"

.PHONY: check-at-acceptance-ibm
check-at-acceptance-ibm: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite acceptance --bios ibm \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --output "$(AT_TEST_DIR)/acceptance-ibm"

.PHONY: check-at-acceptance-ami
check-at-acceptance-ami: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite acceptance --bios ami \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --output "$(AT_TEST_DIR)/acceptance-ami"

.PHONY: check-at-acceptance-full-ibm
check-at-acceptance-full-ibm: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite acceptance --bios ibm \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --full-ram --output "$(AT_TEST_DIR)/acceptance-full-ibm"

.PHONY: check-at-acceptance-full-ami
check-at-acceptance-full-ami: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite acceptance --bios ami \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) --full-ram --output "$(AT_TEST_DIR)/acceptance-full-ami"

.PHONY: check-at-floppy-ibm
check-at-floppy-ibm: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite floppy --bios ibm \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) \
		--cycles "$(AT_SOAK_CYCLES)" --rounds "$(AT_SOAK_ROUNDS)" \
		--output "$(AT_TEST_DIR)/floppy-ibm"

.PHONY: check-at-floppy-ami
check-at-floppy-ami: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite floppy --bios ami \
		--emulator "$(TEST_EMULATOR)" $(AT_TIMEOUT_OPT) \
		--cycles "$(AT_SOAK_CYCLES)" --rounds "$(AT_SOAK_ROUNDS)" \
		--output "$(AT_TEST_DIR)/floppy-ami"

# Optional native HDD media is supplied explicitly, with no sample default.
check-at-disk: $(TEST_EMULATOR)
	$(QR)python3 tests/run_at_tests.py --suite disk --emulator "$(TEST_EMULATOR)" \
		--disk "$(AT_DISK_IMAGE)" $(AT_TIMEOUT_OPT) --output "$(AT_TEST_DIR)/disk"

check-at-pm-soak: check-at-disk
	$(QR)python3 tests/run_at_tests.py --suite pm --emulator "$(TEST_EMULATOR)" \
		--fixture "$(AT_TEST_DIR)/disk" --runs "$(AT_PM_RUNS)" $(AT_TIMEOUT_OPT) \
		--output "$(AT_TEST_DIR)/pm-soak"

check-help:
	@printf '%s\n' \
	 'check-build / check-at-build: compile tests without running them' \
	 'check-unit / check-at-unit: ROM-free C tests' \
	 'check-npx-unit: experimental NPX format, state and CPU-interface tests' \
	 'check-cpu-regression: cross-CPU gate' \
	 'check: C, CPU guest images, CLI exit and native IBM/AMI AT smoke tests' \
	 'check-at: AT component and native smoke tests' \
	 'check-at-boot[-ibm|-ami]: native POST and DOS boot' \
	 'check-at-acceptance[-ibm|-ami]: native keys, floppy writes/readback, reboot' \
	 'check-at-full-ram: boot and acceptance with 15232K extended RAM (long)' \
	 'check-at-floppy-soak: repeated native floppy writes/readback and reboots (long)' \
	 'check-at-disk AT_DISK_IMAGE=/path: disposable native WD1003 disk acceptance' \
	 'check-at-pm-soak AT_DISK_IMAGE=/path: prepare HDD fixture and run CPL0 PM soak' \
	 'check-all: check plus full-RAM/floppy soaks; adds disk/PM when AT_DISK_IMAGE is set' \
	 'Overrides: TEST_EMULATOR, AT_TEST_DIR, AT_TIMEOUT, AT_SOAK_CYCLES, AT_SOAK_ROUNDS, AT_PM_RUNS'

DIST += $(addsuffix .c,$(TEST_BINS)) tests/build_images.sh tests/guest_runner.py tests/run_e2e.py \
 tests/run_at_tests.py tests/test.cfg tests/at-5170.cfg tests/at-5170-ami.cfg \
 tests/asm/tataccept.asm tests/asm/tfloppysoak.asm tests/asm/tpmsoak.asm

# NPX foundation: raw operand transfers; no guest coprocessor advertised yet.
.PHONY: check-npx-unit
check-npx-unit: tests/test_npx_format
	./tests/test_npx_format
check-unit: check-npx-unit
check-build: tests/test_npx_format

tests/test_npx_format: tests/test_npx_format.c src/cpu/e8087/real80.c src/cpu/e8087/real80.h
	$(CC) $(CFLAGS_DEFAULT) -o $@ tests/test_npx_format.c src/cpu/e8087/real80.c
CLN += tests/test_npx_format
DIST += tests/test_npx_format.c src/cpu/e8087/real80.c src/cpu/e8087/real80.h

check-npx-unit: check-npx-state
.PHONY: check-npx-state
check-npx-state: tests/test_npx_state
	./tests/test_npx_state
check-build: tests/test_npx_state

tests/test_npx_state: tests/test_npx_state.c src/cpu/e8087/e8087.c src/cpu/e8087/e8087.h src/cpu/e8087/real80.h
	$(CC) $(CFLAGS_DEFAULT) -o $@ tests/test_npx_state.c src/cpu/e8087/e8087.c
CLN += tests/test_npx_state
DIST += tests/test_npx_state.c src/cpu/e8087/e8087.c src/cpu/e8087/e8087.h

.PHONY: check-npx-cpu
check-npx-unit: check-npx-cpu
check-npx-cpu: tests/test_npx_cpu
	./tests/test_npx_cpu
check-build: tests/test_npx_cpu

tests/test_npx_cpu: tests/test_npx_cpu.c src/cpu/e8087/e8087.c src/cpu/e8087/e8087.h src/cpu/e8087/real80.h $(E8086_HDR) $(E8086_ARC)
	$(CC) $(CFLAGS_DEFAULT) -o $@ tests/test_npx_cpu.c src/cpu/e8087/e8087.c $(E8086_ARC)
CLN += tests/test_npx_cpu
DIST += tests/test_npx_cpu.c

# Initial pinned SoftFloat subset; production machine wiring is not enabled.
NPX_FLOAT_SRC := $(wildcard src/cpu/e8087/softfloat/*.c)
NPX_FLOAT_HDR := $(wildcard src/cpu/e8087/softfloat/*.h)
.PHONY: check-npx-arithmetic
check-npx-unit: check-npx-arithmetic
check-npx-arithmetic: tests/test_npx_arithmetic
	./tests/test_npx_arithmetic
check-build: tests/test_npx_arithmetic
tests/test_npx_arithmetic: tests/test_npx_arithmetic.c src/cpu/e8087/arithmetic.c src/cpu/e8087/arithmetic.h src/cpu/e8087/real80.h $(NPX_FLOAT_SRC) $(NPX_FLOAT_HDR)
	$(CC) $(CFLAGS_DEFAULT) -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5 -Isrc/cpu/e8087/softfloat -o $@ tests/test_npx_arithmetic.c src/cpu/e8087/arithmetic.c $(NPX_FLOAT_SRC)
CLN += tests/test_npx_arithmetic
DIST += tests/test_npx_arithmetic.c src/cpu/e8087/arithmetic.c src/cpu/e8087/arithmetic.h $(NPX_FLOAT_SRC) $(NPX_FLOAT_HDR) src/cpu/e8087/softfloat/COPYING.txt src/cpu/e8087/softfloat/LICENSE.md src/cpu/e8087/softfloat/README.pce

.PHONY: check-npx-execute
check-npx-unit: check-npx-execute
check-npx-execute: tests/test_npx_execute
	./tests/test_npx_execute
check-build: tests/test_npx_execute
tests/test_npx_execute: tests/test_npx_execute.c src/cpu/e8087/execute.c src/cpu/e8087/execute.h src/cpu/e8087/e8087.c src/cpu/e8087/e8087.h src/cpu/e8087/real80.c src/cpu/e8087/real80.h src/cpu/e8087/arithmetic.c src/cpu/e8087/arithmetic.h $(NPX_FLOAT_SRC) $(NPX_FLOAT_HDR)
	$(CC) $(CFLAGS_DEFAULT) -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5 -Isrc/cpu/e8087/softfloat -o $@ tests/test_npx_execute.c src/cpu/e8087/execute.c src/cpu/e8087/e8087.c src/cpu/e8087/real80.c src/cpu/e8087/arithmetic.c $(NPX_FLOAT_SRC)
CLN += tests/test_npx_execute
DIST += tests/test_npx_execute.c src/cpu/e8087/execute.c src/cpu/e8087/execute.h

.PHONY: check-npx-memory
check-npx-unit: check-npx-memory
check-npx-memory: tests/test_npx_memory
	./tests/test_npx_memory
check-build: tests/test_npx_memory
tests/test_npx_memory: tests/test_npx_memory.c src/cpu/e8087/execute.c src/cpu/e8087/execute.h src/cpu/e8087/e8087.c src/cpu/e8087/e8087.h src/cpu/e8087/real80.c src/cpu/e8087/real80.h src/cpu/e8087/arithmetic.c src/cpu/e8087/arithmetic.h $(NPX_FLOAT_SRC) $(NPX_FLOAT_HDR) $(E8086_ARC) $(E8086_HDR)
	$(CC) $(CFLAGS_DEFAULT) -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5 -Isrc/cpu/e8087/softfloat -o $@ tests/test_npx_memory.c src/cpu/e8087/execute.c src/cpu/e8087/e8087.c src/cpu/e8087/real80.c src/cpu/e8087/arithmetic.c $(NPX_FLOAT_SRC) $(E8086_ARC)
CLN += tests/test_npx_memory
DIST += tests/test_npx_memory.c

# Numeric callback adapters use the CPU event contract.
tests/test_npx_execute: $(E8086_HDR)

# Explicit hardware-characterization utility; never launched by make check.
.PHONY: npx-overflow-probe
npx-overflow-probe: tests/npxovf.com
tests/npxovf.com: $(srcdir)/tests/asm/npxovf.asm
	$(NASM) -f bin $< -o $@
CLN += tests/npxovf.com
DIST += tests/asm/npxovf.asm tests/decode_npx_overflow.py tests/npx-overflow-probe.md
