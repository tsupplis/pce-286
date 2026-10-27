# demos/Makefile.inc
#
# Build rules for the 80286 PM visual demo.
#
#   make demo           — build demos/pmshow.img and launch with SDL terminal
#   make demo-headless  — build and run headless (CI-friendly exit-code check)
#

rel_demos := demos

DIRS += $(rel_demos)

DEMO_ASM_FLAGS := -O6 -I$(srcdir)/src/ -I$(srcdir)/src/arch/ibmpc/

DEMO_COM  := $(rel_demos)/pmshow.com
DEMO_IMG  := $(rel_demos)/pmshow.img

CLN += $(DEMO_COM) $(DEMO_IMG)

EMU_BIN_D  := src/arch/ibmpc/pce-ibmpc
EMU_CFG_D  := tests/test.cfg
PCEEXIT_D  := src/arch/ibmpc/pceutils/pceexit.com
DOS_BASE_D := images/dos.img

ifeq "$(PCE_HAVE_NASM)" "1"

# -----------------------------------------------------------------------
# Assemble pmshow.com from pmshow.asm
$(DEMO_COM): $(rel_demos)/pmshow.asm
	$(QP)echo "  NASM   $@"
	$(QR)$(NASM) $(DEMO_ASM_FLAGS) -f bin $< -o $@

# -----------------------------------------------------------------------
# Build pmshow.img: DOS base image + pmshow.com + pceexit.com + AUTOEXEC.BAT
$(DEMO_IMG): $(DEMO_COM) $(PCEEXIT_D) $(DOS_BASE_D)
	$(QP)echo "  IMAGE  $@"
	$(QR)rm -f $@ && cp $(DOS_BASE_D) $@ && chmod 644 $@
	$(QR)printf '@ECHO OFF\r\nPATH A:\\DOS;A:\\\r\nPMSHOW.COM\r\nPCEEXIT.COM\r\n' \
		> /tmp/_pce_demo_autoexec.bat
	$(QR)mcopy -D o -i $@ $(DEMO_COM)      ::/PMSHOW.COM
	$(QR)mcopy -D o -i $@ $(PCEEXIT_D)     ::/PCEEXIT.COM
	$(QR)mcopy -D o -i $@ /tmp/_pce_demo_autoexec.bat ::/AUTOEXEC.BAT
	$(QR)rm -f /tmp/_pce_demo_autoexec.bat

# -----------------------------------------------------------------------
# make demo — build image and launch with SDL terminal (user watches output)
.PHONY: demo
demo: $(DEMO_IMG)
	$(QP)echo "  DEMO   launching pmshow (close the window to exit)"
	$(QR)"$(EMU_BIN_D)" -c "$(EMU_CFG_D)" -p 80286 \
		-I "disk {" -I "drive = 0" \
		-I "type = \"auto\"" \
		-I "file = \"$(DEMO_IMG)\"" \
		-I "}"

# -----------------------------------------------------------------------
# make demo-headless — headless run, exit code = pass/fail (CI use)
.PHONY: demo-headless
demo-headless: $(DEMO_IMG)
	$(QP)echo "  DEMO   pmshow headless ..."
	$(QR)if "$(EMU_BIN_D)" -R -c "$(EMU_CFG_D)" -p 80286 \
		-I "disk {" -I "drive = 0" \
		-I "type = \"auto\"" \
		-I "file = \"$(DEMO_IMG)\"" \
		-I "}" \
		>/dev/null 2>&1 ; then \
		echo "  DEMO   pmshow PASS" ; \
	else \
		echo "  DEMO   pmshow FAIL" ; exit 1 ; \
	fi

else

# NASM not available
.PHONY: demo demo-headless
demo demo-headless:
	@echo "  SKIP   NASM not available; demo not built"

endif
