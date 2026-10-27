# tests/Makefile.inc
#
# C-level unit tests and end-to-end NASM regression tests for the CPU
# emulation.  Driven by:
#   make check         — build everything, run all C-unit tests, then if
#                        pce-ibmpc and images/dos.img exist also run the
#                        NASM end-to-end tests.
#   make check-build   — compile only, do not run anything
#

rel := tests

DIRS += $(rel)
DIST += $(rel)/Makefile.inc $(rel)/test_ibmpc_memory.c $(rel)/test_ibmpc_exit.py $(rel)/test_at_kbd.c

# -----------------------------------------------------------------------
# The e8086 archive (all CPU object files linked into one archive).
E8086_ARC := src/cpu/e8086/e8086.a src/cpu/e8080/e8080.a

# -----------------------------------------------------------------------
# Per-test binary list
TEST_BINS := \
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
	$(ASM_DIR)/tloadall.com

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
EMU_BIN   := src/arch/ibmpc/pce-ibmpc
EMU_CFG   := tests/test.cfg
IMG_T186    := images/t186.img
IMG_T286    := images/t286.img
IMG_TV20    := images/tv20.img
IMG_TA20    := images/ta20.img
IMG_TPROT    := images/tprot.img
IMG_TEXTMEM  := images/textmem.img
IMG_TLOADALL := images/tloadall.img
CLN += $(IMG_T186) $(IMG_T286) $(IMG_TV20) $(IMG_TA20) $(IMG_TPROT) $(IMG_TEXTMEM) $(IMG_TLOADALL)

# 'make check-images' — build the four fixed test floppy images.
# Depends on the .com files and pceexit.com being up to date.
.PHONY: check-images
check-images: $(ASM_TEST_COMS)
	$(QP)echo "  IMAGES build test floppy images"
	$(QR)sh tests/build_images.sh

# -----------------------------------------------------------------------
# Helper: run one emulator test against a fixed image.
# Usage: $(call run_emu, label, cpu, image)
define run_emu
	$(QR)if test -x "$(EMU_BIN)" && test -f "$(3)" ; then \
		printf "  TEST   %-30s cpu=%-8s ... " "$(3)" "$(2)" ; \
		"$(EMU_BIN)" -R -c "$(EMU_CFG)" -p "$(2)" \
			-I "disk {" -I "drive = 0" \
			-I "type = \"auto\"" \
			-I "file = \"$(3)\"" \
			-I "}" \
			>/dev/null 2>&1 \
		&& printf "PASS\n" \
		|| { printf "FAIL\n" ; e2e_fail=1 ; } ; \
	else \
		echo "  SKIP   $(3) (not found — run make check-images first)" ; \
	fi
endef

.PHONY: check-e2e
check-e2e: $(EMU_CFG)
	$(QP)echo "  RUN    end-to-end CPU tests"
	$(QR)e2e_fail=0 ; \
	for cpu in 80186 80188 v20 v30 80286 ; do \
		if test -x "$(EMU_BIN)" && test -f "$(IMG_T186)" ; then \
			printf "  TEST   %-30s cpu=%-8s ... " "$(IMG_T186)" "$$cpu" ; \
			"$(EMU_BIN)" -R -c "$(EMU_CFG)" -p "$$cpu" \
				-I "disk {" -I "drive = 0" \
				-I "type = \"auto\"" \
				-I "file = \"$(IMG_T186)\"" \
				-I "}" \
				>/dev/null 2>&1 \
			&& printf "PASS\n" || { printf "FAIL\n" ; e2e_fail=1 ; } ; \
		else \
			echo "  SKIP   $(IMG_T186) (run make check-images first)" ; \
		fi ; \
	done ; \
	for cpu in v20 v30 ; do \
		if test -x "$(EMU_BIN)" && test -f "$(IMG_TV20)" ; then \
			printf "  TEST   %-30s cpu=%-8s ... " "$(IMG_TV20)" "$$cpu" ; \
			"$(EMU_BIN)" -R -c "$(EMU_CFG)" -p "$$cpu" \
				-I "disk {" -I "drive = 0" \
				-I "type = \"auto\"" \
				-I "file = \"$(IMG_TV20)\"" \
				-I "}" \
				>/dev/null 2>&1 \
			&& printf "PASS\n" || { printf "FAIL\n" ; e2e_fail=1 ; } ; \
		else \
			echo "  SKIP   $(IMG_TV20) (run make check-images first)" ; \
		fi ; \
	done ; \
	for cpu in 80286 ; do \
		for img in "$(IMG_T286)" "$(IMG_TA20)" "$(IMG_TPROT)" "$(IMG_TEXTMEM)" "$(IMG_TLOADALL)" ; do \
			if test -x "$(EMU_BIN)" && test -f "$$img" ; then \
				printf "  TEST   %-30s cpu=%-8s ... " "$$img" "$$cpu" ; \
				"$(EMU_BIN)" -R -c "$(EMU_CFG)" -p "$$cpu" \
					-I "disk {" -I "drive = 0" \
					-I "type = \"auto\"" \
					-I "file = \"$$img\"" \
					-I "}" \
					>/dev/null 2>&1 \
				&& printf "PASS\n" || { printf "FAIL\n" ; e2e_fail=1 ; } ; \
			else \
				echo "  SKIP   $$img (run make check-images first)" ; \
			fi ; \
		done ; \
	done ; \
	if test $$e2e_fail -ne 0 ; then \
		echo "SOME END-TO-END TESTS FAILED." ; \
		exit 1 ; \
	fi ; \
	echo "All end-to-end tests passed (or skipped)."

# Wire both C-unit and e2e into 'make check'
# 'make check' — C unit tests + end-to-end (full suite)
.PHONY: check
check: check-unit check-e2e

else

# NASM not available — skip end-to-end tests gracefully
.PHONY: check-asm-build check-e2e
check-asm-build:
	@echo "  SKIP   NASM not available; ASM tests not built"
check-e2e:
	@echo "  SKIP   NASM not available; end-to-end tests skipped"

endif

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
EXIT_TEST_BIN ?= src/arch/ibmpc/pce-ibmpc
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
