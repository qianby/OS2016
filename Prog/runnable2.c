// os2.c -- test various features

#include <u.h>

enum { // page table entry flags
  PTE_P   = 0x001,       // Present
  PTE_W   = 0x002,       // Writeable
  PTE_U   = 0x004,       // User
//PTE_PWT = 0x008,       // Write-Through
//PTE_PCD = 0x010,       // Cache-Disable
  PTE_A   = 0x020,       // Accessed
  PTE_D   = 0x040,       // Dirty
//PTE_PS  = 0x080,       // Page Size
//PTE_MBZ = 0x180,       // Bits must be zero
};

enum { // processor fault codes
  FMEM,   // bad physical address
  FTIMER, // timer interrupt
  FKEYBD, // keyboard interrupt
  FPRIV,  // privileged instruction
  FINST,  // illegal instruction
  FSYS,   // software trap
  FARITH, // arithmetic trap
  FIPAGE, // page fault on opcode fetch
  FWPAGE, // page fault on write
  FRPAGE, // page fault on read
  USER=16 // user mode exception 
};

char task0_stack[1000];
char task0_kstack[1000];

char task1_stack[1000];
char task1_kstack[1000];

int *task0_sp;
int *task1_sp;

char pg_mem[6 * 4096]; // page dir + 4 entries + alignment

int *pg_dir, *pg0, *pg1, *pg2, *pg3;

int current;

int in(port)    { asm(LL,8); asm(BIN); }
out(port, val)  { asm(LL,8); asm(LBL,16); asm(BOUT); }
ivec(void *isr) { asm(LL,8); asm(IVEC); }
lvadr()         { asm(LVAD); }
stmr(int val)   { asm(LL,8); asm(TIME); }
pdir(value)     { asm(LL,8); asm(PDIR); }
spage(value)    { asm(LL,8); asm(SPAG); }
halt(value)     { asm(LL,8); asm(HALT); }

void *memcpy() { asm(LL,8); asm(LBL, 16); asm(LCL,24); asm(MCPY); asm(LL,8); }
void *memset() { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MSET); asm(LL,8); }
void *memchr() { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MCHR); }

write(fd, char *p, n) { while (n--) out(fd, *p++); }

int strlen(char *s) { return memchr(s, 0, -1) - (void *)s; }

enum { BUFSIZ = 32 };
int vsprintf(char *s, char *f, va_list v)
{
  char *e = s, *p, c, fill, b[BUFSIZ];
  int i, left, fmax, fmin, sign;

  while (c = *f++) {
    if (c != '%') { *e++ = c; continue; }
    if (*f == '%') { *e++ = *f++; continue; }
    if (left = (*f == '-')) f++;
    fill = (*f == '0') ? *f++ : ' ';
    fmin = sign = 0; fmax = BUFSIZ;
    if (*f == '*') { fmin = va_arg(v,int); f++; } else while ('0' <= *f && *f <= '9') fmin = fmin * 10 + *f++ - '0';
    if (*f == '.') { if (*++f == '*') { fmax = va_arg(v,int); f++; } else for (fmax = 0; '0' <= *f && *f <= '9'; fmax = fmax * 10 + *f++ - '0'); }
    if (*f == 'l') f++;
    switch (c = *f++) {
    case 0: *e++ = '%'; *e = 0; return e - s;
    case 'c': fill = ' '; i = (*(p = b) = va_arg(v,int)) ? 1 : 0; break;
    case 's': fill = ' '; if (!(p = va_arg(v,char *))) p = "(null)"; if ((i = strlen(p)) > fmax) i = fmax; break;
    case 'u': i = va_arg(v,int); goto c1;
    case 'd': if ((i = va_arg(v,int)) < 0) { sign = 1; i = -i; } c1: p = b + BUFSIZ-1; do { *--p = ((uint)i % 10) + '0'; } while (i = (uint)i / 10); i = (b + BUFSIZ-1) - p; break;
    case 'o': i = va_arg(v,int); p = b + BUFSIZ-1; do { *--p = (i & 7) + '0'; } while (i = (uint)i >> 3); i = (b + BUFSIZ-1) - p; break;
    case 'p': fill = '0'; fmin = 8; c = 'x';
    case 'x': case 'X': c -= 33; i = va_arg(v,int); p = b + BUFSIZ-1; do { *--p = (i & 15) + ((i & 15) > 9 ? c : '0'); } while (i = (uint)i >> 4); i = (b + BUFSIZ-1) - p; break;
    default: *e++ = c; continue;
    }
    fmin -= i + sign;
    if (sign && fill == '0') *e++ = '-';
    if (!left && fmin > 0) { memset(e, fill, fmin); e += fmin; }
    if (sign && fill == ' ') *e++ = '-';
    memcpy(e, p, i); e += i;
    if (left && fmin > 0) { memset(e, fill, fmin); e += fmin; }
  }
  *e = 0;
  return e - s;
}

int printf(char *f) { static char buf[4096]; return write(1, buf, vsprintf(buf, f, &f)); } // XXX remove static from buf

int save(port)
{
  // asm(SL,16);
  // asm(LBL,16);
  // asm(LL,8);
  // asm(BOUT);
  asm(PSHA);
  asm(POPB);
  asm(LL,8);
  asm(BOUT);
}

trap(int c, int b, int a, int fc, int pc)
{
  printf("TRAP: ");
  switch (fc) {
  case FSYS + USER: printf("FSYS + USER"); break;
  case FPRIV: printf("FPRIV"); break;
  case FINST:  printf("FINST"); break;
  case FRPAGE: printf("FRPAGE [0x%08x]",lvadr()); break;
  case FWPAGE: printf("FWPAGE [0x%08x]",lvadr()); break;
  case FIPAGE: printf("FIPAGE [0x%08x]",lvadr()); break;
  case FSYS:   printf("FSYS"); break;
  case FARITH: printf("FARITH"); break;
  case FMEM:   printf("FMEM [0x%08x]\n",lvadr()); break;
  case FTIMER: printf("FTIMER"); current = 1; stmr(0); break;
  case FKEYBD: //printf("FKEYBD [%c]", in(0)); 
      in(0);save(1);
      break;
  
  default:     printf("other [%d]",fc); break;
  }
}

alltraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  // asm(LUSP); asm(PSHA);
  trap();
  // asm(POPA); asm(SUSP);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

usertraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  asm(LUSP); asm(PSHA);
  trap();
  asm(POPA); asm(SUSP);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

setup_paging()
{
  int i;
  
  pg_dir = (int *)((((int)&pg_mem) + 4095) & -4096);
  pg0 = pg_dir + 1024;
  pg1 = pg0 + 1024;
  pg2 = pg1 + 1024;
  pg3 = pg2 + 1024;
  
  pg_dir[0] = (int)pg0 | PTE_P | PTE_W | PTE_U;  // identity map 16M
  pg_dir[1] = (int)pg1 | PTE_P | PTE_W | PTE_U;
  pg_dir[2] = (int)pg2 | PTE_P | PTE_W | PTE_U;
  pg_dir[3] = (int)pg3 | PTE_P | PTE_W | PTE_U;
  for (i=4;i<1024;i++) pg_dir[i] = 0;
  
  for (i=0;i<4096;i++) pg0[i] = (i<<12) | PTE_P | PTE_W | PTE_U;  // trick to write all 4 contiguous pages
  
  pdir(pg_dir);
  spage(1);
}

task0()
{
  while(current < 1)
    write(1, "00", 2);

  write(1,"task0 exit\n", 11);
  halt(0);
}

task1()
{
  while(current < 10)
    write(1, "11", 2);

  write(1,"task1 exit\n", 11);
  halt(0);
}

swtch(int *old, int new) // switch stacks
{
  asm(LEA, 0); // a = sp
  asm(LBL, 8); // b = old
  asm(SX, 0);  // *b = a
  asm(LL, 16); // a = new
  asm(SSP);    // sp = a
}

trapret()
{
  asm(POPA); asm(SUSP);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

main()
{
  int t, d,addr; 
  int *kstack;
  current = 0;
  ivec(alltraps);
  
  asm(STI);
  
  printf("test timer...");
  t = 0;
  addr = 0x0;
  stmr(5000);
  while (!current) t++;
  printf("(t=%d)...ok\n",t);
  
  printf("test invalid instruction...");
//  asm(_dd); // XXX find a better way
  asm(-1);
  printf("...ok\n");
  
  printf("test bad physical address...");
  t = *(int *)0x20000000;
  printf("...ok\n");

  printf("test divide by zero...");
  t = 10; d = 0; t /= d;
  printf("...ok\n");
  
  printf("test paging...");
  // reposition stack within first 16M
  asm(LI, 4*1024*1024); // a = 4M
  asm(SSP); // sp = a
  setup_paging();
  printf("identity map...ok\n");

  printf("test page fault read...");
  pg0[50] = 0;
  pdir(pg_dir);
  t = *(int *)(50<<12);
  printf("...ok\n");

  printf("test page fault write...");
  *(int *)(50<<12) = 5;
  printf("...ok\n");

  printf("test software interrupt...");
  asm(TRAP);
  printf("...ok\n");

  // asm(CLI);
  // asm(STI);
  // ivec(usertraps);
  // task0_sp = &task0_kstack[1000];
  // task0_sp -= 2; *task0_sp = &task0;
  // task0_sp -= 2; *task0_sp = USER; // fault code
  // task0_sp -= 2; *task0_sp = 0; // a
  // task0_sp -= 2; *task0_sp = 0; // b
  // task0_sp -= 2; *task0_sp = 0; // c
  // task0_sp -= 2; *task0_sp = &task0_stack[1000];
  // task0_sp -= 2; *task0_sp = &trapret;  
  
  // task1_sp = &task1_kstack[1000];
  // task1_sp -= 2; *task1_sp = &task1;
  // task1_sp -= 2; *task1_sp = USER; // fault code
  // task1_sp -= 2; *task1_sp = 0; // a
  // task1_sp -= 2; *task1_sp = 0; // b
  // task1_sp -= 2; *task1_sp = 0; // c
  // task1_sp -= 2; *task1_sp = &task1_stack[1000];
  // task1_sp -= 2; *task1_sp = &trapret;

  // kstack = task0_sp;
  
  // asm(LL, 4); // a = kstack
  // asm(SSP);   // sp = a
  // asm(LEV);

  // asm(IDLE);
  // while(1);
  halt(0);
}
