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
void native_live_cc_0(Z80State*);void native_live_cc_1(Z80State*);
void native_live_cc_2(Z80State*);void native_live_cc_3(Z80State*);
void native_live_cc_4(Z80State*);void native_live_cc_5(Z80State*);
void native_live_cc_6(Z80State*);void native_live_cc_7(Z80State*);
#define ALU_DECL(n) void native_live_alu_##n(Z80State*);void native_const_alu_##n(Z80State*);
ALU_DECL(0) ALU_DECL(1) ALU_DECL(2) ALU_DECL(3) ALU_DECL(4) ALU_DECL(5) ALU_DECL(6) ALU_DECL(7)
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
 void (*jumps[8])(Z80State*)={native_live_cc_0,native_live_cc_1,native_live_cc_2,native_live_cc_3,
  native_live_cc_4,native_live_cc_5,native_live_cc_6,native_live_cc_7};
 const unsigned flags[4]={Z80_FLAG_Z,Z80_FLAG_C,Z80_FLAG_P,Z80_FLAG_S};
 for(unsigned cc=0;cc<8;++cc)for(unsigned set=0;set<2;++set){
  memset(&a,0,sizeof a);a.pc=0x100;a.r=0x7f;a.f=set?0xff:0;a.cyc=7;a.sp=0xfe00;
  memory[0x100]=0xc2|(cc<<3);memory[0x101]=0x56;memory[0x102]=0x34;
  const int taken=((a.f&flags[cc>>1])!=0)==(int)(cc&1);
  jumps[cc](&a);CHECK(a.pc==(taken?0x3456:0x103) && a.r==0 && a.cyc==17 && a.f==(set?0xff:0) && a.sp==0xfe00);
  b=a;memory[0x100]=0;jumps[cc](&a);CHECK(!memcmp(&a,&b,sizeof a));}
 void (*live_alu[8])(Z80State*)={native_live_alu_0,native_live_alu_1,native_live_alu_2,native_live_alu_3,
  native_live_alu_4,native_live_alu_5,native_live_alu_6,native_live_alu_7};
 void (*const_alu[8])(Z80State*)={native_const_alu_0,native_const_alu_1,native_const_alu_2,native_const_alu_3,
  native_const_alu_4,native_const_alu_5,native_const_alu_6,native_const_alu_7};
 for(unsigned alu=0;alu<8;++alu)for(unsigned carry=0;carry<2;++carry)for(unsigned v=0;v<3;++v){
  memset(&a,0,sizeof a);a.pc=0x100;a.r=0xff;a.f=carry?0xff:0;a.a=v==0?0:v==1?0x7f:0xff;a.cyc=17;
  b=a;memory[0x100]=0xc6|(alu<<3);memory[0x101]=0x2c;
  live_alu[alu](&a);const_alu[alu](&b);CHECK(!memcmp(&a,&b,sizeof a));
  b=a;memory[0x100]=0;live_alu[alu](&a);CHECK(!memcmp(&a,&b,sizeof a));}
 return 0;}
