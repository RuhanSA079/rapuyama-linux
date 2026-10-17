// memdump <hexaddr> <hexlen>: phys mem -> stdout through an mmap of /dev/mem.
// read() on /dev/mem refuses everything below PHYS_OFFSET on arm
// (valid_phys_addr_range), mmap doesn't. no libc, the dram_dump.py uploads it
// over the usb shell
// build: arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -Os -o memdump memdump.c

static long sys(long n, long a, long b, long c, long d, long e, long f){
	register long r0 asm("r0")=a, r1 asm("r1")=b, r2 asm("r2")=c, r3 asm("r3")=d, r4 asm("r4")=e, r5 asm("r5")=f, r7 asm("r7")=n;
	asm volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory");
	return r0;
}

static unsigned long hex(const char *s){
	unsigned long v=0;
	if(s[0]=='0' && s[1]=='x'){s+=2;}
	for(; *s; s++){v=v*16+(*s<='9' ? *s-'0' : (*s|0x20)-'a'+10);}
	return v;
}

static unsigned int buf[0x4000/4];

static int run(int argc, char **argv){
	unsigned long a, n, i, j;
	long fd;
	if(argc<3){return 1;}
	a=hex(argv[1]);
	n=hex(argv[2]);
	fd=sys(5, (long)"/dev/mem", 0x101000, 0, 0, 0, 0); // open O_RDONLY|O_SYNC
	if(fd<0){return 2;}
	for(i=0; i<n; i+=sizeof(buf)){
		// mmap2 takes the offset in pages. device memory, so copy word by word
		volatile unsigned int *p=(void *)sys(192, 0, sizeof(buf), 1, 1, fd, (a+i)>>12);
		if((unsigned long)p>=0xfffff000ul){return 3;}
		for(j=0; j<sizeof(buf)/4; j++){buf[j]=p[j];}
		sys(91, (long)p, sizeof(buf), 0, 0, 0, 0); // munmap
		for(j=0; j<sizeof(buf);){
			long w=sys(4, 1, (long)buf+j, sizeof(buf)-j, 0, 0, 0);
			if(w<=0){return 4;}
			j+=w;
		}
	}
	return 0;
}

__attribute__((naked, noreturn)) void _start(void){
	// argc at [sp], argv right after it
	asm volatile("ldr r0, [sp]\n add r1, sp, #4\n bl run_exit");
}

void __attribute__((used, noreturn)) run_exit(int argc, char **argv){
	sys(1, run(argc, argv), 0, 0, 0, 0, 0);
	for(;;){}
}