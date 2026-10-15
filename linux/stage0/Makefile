# builds the position-independent stage0 blobs with the bare-metal arm toolchain
# output per blob: .bin (raw arm code, memcpy-able), .hdr (c array that gets
# embedded into the symbian loader driver), .dis (disassembly to look at)
#
# target cpu is the arm1176jzf-s (armv6) in rapuyama. built as generic armv6,
# no libc, no startup, freestanding

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump

CFLAGS   = -march=armv6 -marm -mfloat-abi=soft -ffreestanding -nostdlib \
           -fno-builtin -Os -Wall -Wextra
LDFLAGS  = -nostdlib -T blob.lds

all: blob.bin blob.hdr blob.dis stub2.hdr stub2.dis \
     tramp3.hdr tramp3.dis fakekern.hdr fakekern.dis \
     takeover.hdr takeover.dis tramp_boot.hdr tramp_boot.dis \
     kickloop.hdr kickloop.dis securetrace.hdr securetrace.dis \
     smcprobe.hdr smcprobe.dis \
     hook.hdr hook.dis

blob.o: blob.S
	$(CC) $(CFLAGS) -c $< -o $@

blob.elf: blob.o blob.lds
	$(LD) $(LDFLAGS) blob.o -o $@

blob.bin: blob.elf
	$(OBJCOPY) -O binary $< $@
	@echo "size: $$(wc -c < $@) bytes"

blob.dis: blob.elf
	$(OBJDUMP) -d $< > $@

# stub2: reversible mmu-off trampoline stub (entry symbol stub2)
stub2.o: stub2.S
	$(CC) $(CFLAGS) -c $< -o $@

stub2.elf: stub2.o blob.lds
	$(LD) -nostdlib -Ttext 0 --entry stub2 stub2.o -o $@

stub2.bin: stub2.elf
	$(OBJCOPY) -O binary $< $@
	@echo "stub2 size: $$(wc -c < $@) bytes"

stub2.dis: stub2.elf
	$(OBJDUMP) -d $< > $@

stub2.hdr: stub2.bin
	@printf '// auto-generated from stub2.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KStub2[] = {\n' >> $@
	@od -An -v -tu1 stub2.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KStub2Len = sizeof(KStub2);\n' >> $@
	@echo "wrote $@"

# tramp3: full handoff trampoline
tramp3.o: tramp3.S
	$(CC) $(CFLAGS) -c $< -o $@
tramp3.elf: tramp3.o
	$(LD) -nostdlib -Ttext 0 --entry tramp_entry tramp3.o -o $@
tramp3.bin: tramp3.elf
	$(OBJCOPY) -O binary $< $@
	@echo "tramp3 size: $$(wc -c < $@) bytes"
tramp3.dis: tramp3.elf
	$(OBJDUMP) -d $< > $@
tramp3.hdr: tramp3.bin
	@printf '// auto-generated from tramp3.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KTramp3[] = {\n' >> $@
	@od -An -v -tu1 tramp3.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KTramp3Len = sizeof(KTramp3);\n' >> $@
	@echo "wrote $@"

# fakekern: fake kernel (payload at +0x8000)
fakekern.o: fakekern.S
	$(CC) $(CFLAGS) -c $< -o $@
fakekern.elf: fakekern.o
	$(LD) -nostdlib -Ttext 0 --entry fake_kernel fakekern.o -o $@
fakekern.bin: fakekern.elf
	$(OBJCOPY) -O binary $< $@
	@echo "fakekern size: $$(wc -c < $@) bytes"
fakekern.dis: fakekern.elf
	$(OBJDUMP) -d $< > $@
fakekern.hdr: fakekern.bin
	@printf '// auto-generated from fakekern.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KFakeKern[] = {\n' >> $@
	@od -An -v -tu1 fakekern.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KFakeKernLen = sizeof(KFakeKern);\n' >> $@
	@echo "wrote $@"

# takeover: one-way kill symbian and drive the display
takeover.o: takeover.S
	$(CC) $(CFLAGS) -c $< -o $@
takeover.elf: takeover.o
	$(LD) -nostdlib -Ttext 0 --entry takeover takeover.o -o $@
takeover.bin: takeover.elf
	$(OBJCOPY) -O binary $< $@
	@echo "takeover size: $$(wc -c < $@) bytes"
takeover.dis: takeover.elf
	$(OBJDUMP) -d $< > $@
takeover.hdr: takeover.bin
	@printf '// auto-generated from takeover.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KTakeover[] = {\n' >> $@
	@od -An -v -tu1 takeover.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KTakeoverLen = sizeof(KTakeover);\n' >> $@
	@echo "wrote $@"

# tramp_boot: one-way zImage handoff stub (source is in ../boot)
tramp_boot.o: ../boot/tramp_boot.S
	$(CC) $(CFLAGS) -c $< -o $@
tramp_boot.elf: tramp_boot.o
	$(LD) -nostdlib -Ttext 0 --entry tramp_boot tramp_boot.o -o $@
tramp_boot.bin: tramp_boot.elf
	$(OBJCOPY) -O binary $< $@
	@echo "tramp_boot size: $$(wc -c < $@) bytes"
tramp_boot.dis: tramp_boot.elf
	$(OBJDUMP) -d $< > $@
tramp_boot.hdr: tramp_boot.bin
	@printf '// auto-generated from tramp_boot.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KTrampBoot[] = {\n' >> $@
	@od -An -v -tu1 tramp_boot.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KTrampBootLen = sizeof(KTrampBoot);\n' >> $@
	@echo "wrote $@"

# c header: blob.bin as a byte array for the symbian loader to copy+run
blob.hdr: blob.bin
	@printf '// auto-generated from blob.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KStage0Blob[] = {\n' >> $@
	@od -An -v -tu1 blob.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KStage0BlobLen = sizeof(KStage0Blob);\n' >> $@
	@echo "wrote $@"

clean:
	rm -f blob.o blob.elf blob.bin blob.hdr blob.dis \
	      stub2.o stub2.elf stub2.bin stub2.hdr stub2.dis \
	      tramp3.o tramp3.elf tramp3.bin tramp3.hdr tramp3.dis \
	      fakekern.o fakekern.elf fakekern.bin fakekern.hdr fakekern.dis \
	      takeover.o takeover.elf takeover.bin takeover.hdr takeover.dis \
	      tramp_boot.o tramp_boot.elf tramp_boot.bin tramp_boot.hdr tramp_boot.dis \
	      kickloop.o kickloop.elf kickloop.bin kickloop.hdr kickloop.dis \
	      securetrace.o securetrace.elf securetrace.bin securetrace.hdr securetrace.dis \
	      smcprobe.o smcprobe.elf smcprobe.bin smcprobe.hdr smcprobe.dis \
	      hook.o hook.elf hook.bin hook.hdr hook.dis

.PHONY: all clean

# kickloop: one-way, mmu off, kicks both watchdogs forever (watchdog test)
kickloop.o: kickloop.S
	$(CC) $(CFLAGS) -c $< -o $@
kickloop.elf: kickloop.o
	$(LD) -nostdlib -Ttext 0 --entry kickloop kickloop.o -o $@
kickloop.bin: kickloop.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kickloop size: $$(wc -c < $@) bytes"
kickloop.dis: kickloop.elf
	$(OBJDUMP) -d $< > $@
kickloop.hdr: kickloop.bin
	@printf '// auto-generated from kickloop.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KKickLoop[] = {\n' >> $@
	@od -An -v -tu1 kickloop.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KKickLoopLen = sizeof(KKickLoop);\n' >> $@
	@echo "wrote $@"

# securetrace: watchdog-fed bare-metal loop with a fast-counter heartbeat log
securetrace.o: securetrace.S
	$(CC) $(CFLAGS) -c $< -o $@
securetrace.elf: securetrace.o
	$(LD) -nostdlib -Ttext 0 --entry securetrace securetrace.o -o $@
securetrace.bin: securetrace.elf
	$(OBJCOPY) -O binary $< $@
	@echo "securetrace size: $$(wc -c < $@) bytes"
securetrace.dis: securetrace.elf
	$(OBJDUMP) -d $< > $@
securetrace.hdr: securetrace.bin
	@printf '// auto-generated from securetrace.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KSecureTrace[] = {\n' >> $@
	@od -An -v -tu1 securetrace.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KSecureTraceLen = sizeof(KSecureTrace);\n' >> $@
	@echo "wrote $@"

# smcprobe: one-way, mmu off, feeds the watchdogs then does one smc #0 and logs
# whether the secure monitor returned (secure-call feasibility probe)
smcprobe.o: smcprobe.S
	$(CC) $(CFLAGS) -c $< -o $@
smcprobe.elf: smcprobe.o
	$(LD) -nostdlib -Ttext 0 --entry smcprobe smcprobe.o -o $@
smcprobe.bin: smcprobe.elf
	$(OBJCOPY) -O binary $< $@
	@echo "smcprobe size: $$(wc -c < $@) bytes"
smcprobe.dis: smcprobe.elf
	$(OBJDUMP) -d $< > $@
smcprobe.hdr: smcprobe.bin
	@printf '// auto-generated from smcprobe.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KSmcProbe[] = {\n' >> $@
	@od -An -v -tu1 smcprobe.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KSmcProbeLen = sizeof(KSmcProbe);\n' >> $@
	@echo "wrote $@"

# hook: transport-fn trampoline that logs each secure request to a ram ring
hook.o: hook.S
	$(CC) $(CFLAGS) -c $< -o $@
hook.elf: hook.o
	$(LD) -nostdlib -Ttext 0 --entry hooktransport hook.o -o $@
hook.bin: hook.elf
	$(OBJCOPY) -O binary $< $@
	@echo "hook size: $$(wc -c < $@) bytes"
hook.dis: hook.elf
	$(OBJDUMP) -d $< > $@
hook.hdr: hook.bin
	@printf '// auto-generated from hook.bin -- do not edit\n' > $@
	@printf 'static const unsigned char KHook[] = {\n' >> $@
	@od -An -v -tu1 hook.bin | sed 's/[0-9][0-9]*/&,/g; s/^/    /' >> $@
	@printf '};\n' >> $@
	@printf 'static const unsigned KHookLen = sizeof(KHook);\n' >> $@
	@echo "wrote $@"