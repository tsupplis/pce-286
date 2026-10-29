# src/arch/ibmpc/Makefile.inc

rel := src/arch/ibmpc

DIRS += $(rel)
DIST += $(rel)/Makefile.inc

# ----------------------------------------------------------------------

PCE_IBMPC_CFLAGS  := $(CFLAGS_DEFAULT)
PCE_IBMPC_LDFLAGS := $(LDFLAGS_DEFAULT)
PCE_IBMPC_LIBS    := $(LIBS)

ifeq "$(PCE_ENABLE_SDL)" "1"
	PCE_IBMPC_CFLAGS += $(PCE_SDL_CFLAGS)
	PCE_IBMPC_LIBS += $(PCE_SDL_LIBS)
endif

ifeq "$(PCE_ENABLE_X11)" "1"
	PCE_IBMPC_CFLAGS += $(PCE_X11_CFLAGS)
	PCE_IBMPC_LIBS += $(PCE_X11_LIBS)
endif

ifeq "$(PCE_HOST_WINDOWS)" "1"
	PCE_IBMPC_LIBS += -mconsole
endif

ifeq "$(PCE_ENABLE_READLINE)" "1"
	PCE_IBMPC_LIBS += $(PCE_READLINE_LIBS)
endif

$(rel)/%.o: $(rel)/%.c
	$(QP)echo "  CC     $@"
	$(QR)$(CC) -c $(PCE_IBMPC_CFLAGS) -o $@ $<

# ----------------------------------------------------------------------

PCE_IBMPC_BAS  := \
	npx \
	at-keyboard \
	atari-pc \
	cmd \
	covox \
	ems \
	hook \
	ibmpc \
	int13 \
	keyboard \
	m24 \
	main \
	msg \
	speaker \
	xms

PCE_IBMPC_SRC  := $(foreach f,$(PCE_IBMPC_BAS),$(rel)/$(f).c)
PCE_IBMPC_OBJ  := $(foreach f,$(PCE_IBMPC_BAS),$(rel)/$(f).o)
PCE_IBMPC_HDR  := $(foreach f,$(PCE_IBMPC_BAS),$(rel)/$(f).h)
PCE_IBMPC_MAN1 := $(rel)/pce-ibmpc.1
PCE_IBMPC_ETC  := $(rel)/pce-ibmpc.cfg
PCE_IBMPC_BIN  := $(rel)/pce-ibmpc$(EXEEXT)

NPX_CORE_SRC := $(wildcard $(srcdir)/src/cpu/e8087/*.c) $(wildcard $(srcdir)/src/cpu/e8087/softfloat/*.c)
NPX_CORE_OBJ := $(patsubst $(srcdir)/%.c,%.o,$(NPX_CORE_SRC))
NPX_CORE_HDR := $(wildcard $(srcdir)/src/cpu/e8087/*.h) $(wildcard $(srcdir)/src/cpu/e8087/softfloat/*.h)
DIRS += src/cpu/e8087 src/cpu/e8087/softfloat
CLN += $(NPX_CORE_OBJ)
DIST += $(patsubst $(srcdir)/%,%,$(NPX_CORE_SRC) $(NPX_CORE_HDR))
$(NPX_CORE_OBJ): $(NPX_CORE_HDR) src/cpu/e8086/e8086.h
src/cpu/e8087/%.o: $(srcdir)/src/cpu/e8087/%.c
	$(CC) $(CFLAGS_DEFAULT) -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5 -I$(srcdir)/src/cpu/e8087/softfloat -c -o $@ $<

PCE_IBMPC_OBJ_EXT := $(NPX_CORE_OBJ) \
	src/chipset/clock/mc146818a.o \
	src/chipset/82xx/e8237.o \
	src/chipset/82xx/e8250.o \
	src/chipset/82xx/e8253.o \
	src/chipset/82xx/e8255.o \
	src/chipset/82xx/e8259.o \
	src/chipset/82xx/e8272.o \
	src/chipset/e6845.o \
	src/devices/cassette.o \
	src/devices/device.o \
	src/devices/fdc.o \
	src/devices/hdc.o \
	src/devices/wd1003.o \
	src/devices/memory.o \
	src/devices/nvram.o \
	src/devices/parport.o \
	src/devices/serport.o \
	src/drivers/options.o \
	src/lib/brkpt.o \
	src/lib/cfg.o \
	src/lib/ciff.o \
	src/lib/cmd.o \
	src/lib/console.o \
	src/lib/endian.o \
	src/lib/getopt.o \
	src/lib/inidsk.o \
	src/lib/iniram.o \
	src/lib/initerm.o \
	src/lib/log.o \
	src/lib/monitor.o \
	src/lib/msg.o \
	src/lib/msgdsk.o \
	src/lib/path.o \
	src/lib/string.o \
	src/lib/sysdep.o \
	src/lib/text.o \
	$(LIBPCE_LOAD_OBJ) \
	$(LIBINI_OBJ) \
	$(CPU_8086_OBJ) \
	$(DEV_VID_OBJ) \
	$(DRV_BLK_OBJ) \
	$(DRV_PRI_OBJ) \
	$(DRV_PSI_OBJ) \
	$(DRV_PTI_OBJ) \
	$(DRV_CHR_OBJ) \
	$(DRV_SND_OBJ) \
	$(DRV_TRM_OBJ)

ifeq "$(PCE_ENABLE_TUN)" "1"
PCE_IBMPC_OBJ_EXT += src/lib/tun.o
endif

ifeq "$(PCE_BUILD_IBMPC)" "1"
BIN  += $(PCE_IBMPC_BIN)
ETC  += $(PCE_IBMPC_ETC)
MAN1 += $(PCE_IBMPC_MAN1)
endif

CLN  += $(PCE_IBMPC_BIN) $(PCE_IBMPC_OBJ) $(PCE_IBMPC_ETC)
DIST += $(PCE_IBMPC_SRC) $(PCE_IBMPC_HDR) $(PCE_IBMPC_MAN1)
DIST += $(rel)/pce-ibmpc.cfg.in $(rel)/pce.inc $(rel)/pce-lib.inc

$(rel)/cmd.o:		$(rel)/cmd.c
$(rel)/covox.o:		$(rel)/covox.c
$(rel)/ems.o:		$(rel)/ems.c
$(rel)/hook.o:		$(rel)/hook.c
$(rel)/ibmpc.o:		$(rel)/ibmpc.c
$(rel)/int13.o:		$(rel)/int13.c
$(rel)/keyboard.o:	$(rel)/keyboard.c
$(rel)/m24.o:		$(rel)/m24.c
$(rel)/main.o:		$(rel)/main.c
$(rel)/msg.o:		$(rel)/msg.c
$(rel)/speaker.o:	$(rel)/speaker.c
$(rel)/xms.o:		$(rel)/xms.c

$(rel)/pce-ibmpc$(EXEEXT): $(PCE_IBMPC_OBJ_EXT) $(PCE_IBMPC_OBJ)
	$(QP)echo "  LD     $@"
	$(QR)$(LD) $(PCE_IBMPC_LDFLAGS) -o $@ $(PCE_IBMPC_OBJ) $(PCE_IBMPC_OBJ_EXT) $(PCE_IBMPC_LIBS) -lm

$(rel)/pce-ibmpc.cfg: $(rel)/pce-ibmpc.cfg.in
	$(QP)echo "  SED    $@"
	$(QR)sed -e "s]PCE_DIR_DATA]$(datadir)]g" < $< > $@

# All machine-layer objects share ibmpc_t; rebuild after layout changes.
$(PCE_IBMPC_OBJ): src/arch/ibmpc/ibmpc.h src/arch/ibmpc/at-keyboard.h

$(PCE_IBMPC_OBJ): src/devices/wd1003.h

$(PCE_IBMPC_OBJ): src/devices/fdc.h src/chipset/82xx/e8272.h

$(PCE_IBMPC_OBJ): src/arch/ibmpc/npx.h src/cpu/e8087/e8087.h

# The normal IBM PC binary now includes the BSD-licensed SoftFloat subset.
.PHONY: install-npx-license
ifeq "$(PCE_BUILD_IBMPC)" "1"
install: install-npx-license
install-npx-license:
	$(INSTALL) -d -m 755 "$(DESTDIR)$(datadir)/licenses/softfloat"
	$(INSTALL) -m 644 "$(srcdir)/src/cpu/e8087/softfloat/LICENSE.md" "$(DESTDIR)$(datadir)/licenses/softfloat/LICENSE.md"
endif
