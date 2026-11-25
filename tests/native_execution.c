#include "z80_ops.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x))abort();}while(0)
static uint8_t memory[65536];
uint8_t sms_read8(uint16_t a){return memory[a];}
void sms_write8(uint16_t a,uint8_t v){memory[a]=v;}
uint8_t sms_io_in(uint8_t p){return p;}
void sms_io_out(uint8_t p,uint8_t v){(void)p;(void)v;}
void native_0(Z80State*);void native_1(Z80State*);void native_2(Z80State*);void native_3(Z80State*);
void native_4(Z80State*);void native_5(Z80State*);void native_6(Z80State*);void native_7(Z80State*);
void native_live_jump(Z80State*);
int main(void){Z80State a={0},b={0};native_0(&a);CHECK(a.a==0x7f && a.pc==0x102 && a.cyc==7);
 native_1(&a);CHECK(a.a==0x80 && (a.f&Z80_FLAG_P));CHECK(b.a==0 && b.cyc==0);
 native_2(&a);CHECK(a.ix==0x1000);native_3(&a);CHECK(memory[0xfff]==0x42);
 a.sp=0xff00;native_4(&a);CHECK(a.pc==0x1234 && a.sp==0xfefe && sms_read16(a.sp)==0x103);
 native_5(&a);CHECK(a.pc==0x103 && a.sp==0xff00);
 native_6(&a);CHECK(!(a.f&Z80_FLAG_Z));
 z80_set_hl(&a,0x200);z80_set_de(&a,0x300);z80_set_bc(&a,2);memory[0x200]=0xaa;memory[0x201]=0xbb;
 native_7(&a);CHECK(z80_bc(&a)==1 && a.pc==0x100 && memory[0x300]==0xaa);
 native_7(&a);CHECK(z80_bc(&a)==0 && a.pc==0x102 && memory[0x301]==0xbb);
 memset(&a,0,sizeof a);a.pc=0x100;a.r=0xff;a.f=0xa5;a.cyc=17;a.sp=0xfe00;
 memory[0x100]=0xc3;memory[0x101]=0x56;memory[0x102]=0x34;
 native_live_jump(&a);CHECK(a.pc==0x3456 && a.r==0x80 && a.cyc==27 && a.f==0xa5 && a.sp==0xfe00);
 b=a;memory[0x100]=0;native_live_jump(&a);CHECK(!memcmp(&a,&b,sizeof a));
 return 0;}
