/* Architecture emitter extracted from smsggrecomp; see PROVENANCE.md. */
#include "z80_codegen.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
static _Thread_local jmp_buf emitter_escape;
/* rotating scratch buffers for composed operand expressions */
#define SBSZ 320
static char *sbuf(void){ static _Thread_local char pool[8][SBSZ]; static _Thread_local int i=0; i=(i+1)&7; return pool[i]; }

/* Hard generation error - never emit a stub (PRINCIPLES #12). */
static void cg_fail(const Z80Insn *in){
    fprintf(stderr,
        "[cg_emit] FATAL: unhandled opcode (pfx=%d op=0x%02X \"%s\") - "
        "no stub allowed (PRINCIPLES #12). Fix the emitter.\n",
        in->prefix, in->opcode, in->text);
    longjmp(emitter_escape, 1);
}

/* ---- register operand expressions ---- */
/* 8-bit register names by field index (6 = (HL)/(IX+d), handled separately). */
static const char *RN8[8] = {"s->b","s->c","s->d","s->e","s->h","s->l","","s->a"};

typedef struct {
    const char *idx;        /* "s->ix" / "s->iy" under DD/FD, else NULL */
    bool        uses_disp;  /* DD/FD memory form ((IX+d)) */
    int         disp;
} OpCtx;

/* Does this (unprefixed) base opcode reference (HL)? (Mirror of the decoder's
 * predicate; under DD/FD the same set become (IX+d)/(IY+d).) */
static bool uses_hlmem(uint8_t op){
    if (op==0x34 || op==0x35 || op==0x36) return true;   /* INC/DEC/LD (HL),n */
    if (op==0x76) return false;                           /* HALT */
    if ((op & 0xC7)==0x46) return true;                   /* LD r,(HL) */
    if ((op & 0xF8)==0x70) return true;                   /* LD (HL),r */
    if ((op & 0xC7)==0x86) return true;                   /* ALU A,(HL) */
    return false;
}

/* Read expression for 8-bit field f. Memory operands assume a local `ea`. */
static const char *r8r(const OpCtx *c, int f){
    char *b = sbuf();
    if (f==6) snprintf(b,SBSZ,"sms_read8(ea)");
    else if (c->idx && !c->uses_disp && f==4) snprintf(b,SBSZ,"(uint8_t)(%s >> 8)", c->idx);
    else if (c->idx && !c->uses_disp && f==5) snprintf(b,SBSZ,"(uint8_t)(%s)", c->idx);
    else snprintf(b,SBSZ,"%s", RN8[f]);
    return b;
}
/* Full write statement for 8-bit field f := (val). */
static const char *r8w(const OpCtx *c, int f, const char *val){
    char *b = sbuf();
    if (f==6) snprintf(b,SBSZ,"sms_write8(ea, (uint8_t)(%s));", val);
    else if (c->idx && !c->uses_disp && f==4)
        snprintf(b,SBSZ,"%s = (uint16_t)((%s & 0x00FFu) | ((uint16_t)(uint8_t)(%s) << 8));", c->idx,c->idx,val);
    else if (c->idx && !c->uses_disp && f==5)
        snprintf(b,SBSZ,"%s = (uint16_t)((%s & 0xFF00u) | (uint8_t)(%s));", c->idx,c->idx,val);
    else snprintf(b,SBSZ,"%s = (uint8_t)(%s);", RN8[f], val);
    return b;
}

/* 16-bit pair (rp table: 0 BC,1 DE,2 HL/IX,3 SP). */
static const char *rpr(const OpCtx *c, int p){
    char *b = sbuf();
    switch (p){
        case 0: snprintf(b,SBSZ,"z80_bc(s)"); break;
        case 1: snprintf(b,SBSZ,"z80_de(s)"); break;
        case 2: snprintf(b,SBSZ,"%s", c->idx ? c->idx : "z80_hl(s)"); break;
        default:snprintf(b,SBSZ,"s->sp"); break;
    }
    return b;
}
static const char *rpw(const OpCtx *c, int p, const char *val){
    char *b = sbuf();
    switch (p){
        case 0: snprintf(b,SBSZ,"z80_set_bc(s, %s);", val); break;
        case 1: snprintf(b,SBSZ,"z80_set_de(s, %s);", val); break;
        case 2:
            if (c->idx) snprintf(b,SBSZ,"%s = (uint16_t)(%s);", c->idx, val);
            else        snprintf(b,SBSZ,"z80_set_hl(s, %s);", val);
            break;
        default:snprintf(b,SBSZ,"s->sp = (uint16_t)(%s);", val); break;
    }
    return b;
}
/* 16-bit pair for PUSH/POP (rp2 table: 0 BC,1 DE,2 HL/IX,3 AF). */
static const char *rp2r(const OpCtx *c, int p){
    char *b = sbuf();
    switch (p){
        case 0: snprintf(b,SBSZ,"z80_bc(s)"); break;
        case 1: snprintf(b,SBSZ,"z80_de(s)"); break;
        case 2: snprintf(b,SBSZ,"%s", c->idx ? c->idx : "z80_hl(s)"); break;
        default:snprintf(b,SBSZ,"z80_af(s)"); break;
    }
    return b;
}
static const char *rp2w(const OpCtx *c, int p, const char *val){
    char *b = sbuf();
    switch (p){
        case 0: snprintf(b,SBSZ,"z80_set_bc(s, %s);", val); break;
        case 1: snprintf(b,SBSZ,"z80_set_de(s, %s);", val); break;
        case 2:
            if (c->idx) snprintf(b,SBSZ,"%s = (uint16_t)(%s);", c->idx, val);
            else        snprintf(b,SBSZ,"z80_set_hl(s, %s);", val);
            break;
        default:snprintf(b,SBSZ,"z80_set_af(s, %s);", val); break;
    }
    return b;
}

/* ---- condition codes ---- */
static const char *cond(int cc){
    static const char *C[8] = {
        "!(s->f & Z80_FLAG_Z)","(s->f & Z80_FLAG_Z)",
        "!(s->f & Z80_FLAG_C)","(s->f & Z80_FLAG_C)",
        "!(s->f & Z80_FLAG_P)","(s->f & Z80_FLAG_P)",
        "!(s->f & Z80_FLAG_S)","(s->f & Z80_FLAG_S)",
    };
    return C[cc & 7];
}

/* ---- T-state cost (NMOS Z80; conditional taken-extra added at the branch) -- */
static const uint8_t base_cyc[256] = {
/*       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*00*/   4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
/*10*/   8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
/*20*/   7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
/*30*/   7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
/*40*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*50*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*60*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*70*/   7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
/*80*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*90*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*A0*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*B0*/   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/*C0*/   5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
/*D0*/   5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
/*E0*/   5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
/*F0*/   5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11,
};

static int ed_cyc(uint8_t op){
    if (op==0xB0||op==0xB8||op==0xB1||op==0xB9||
        op==0xB2||op==0xBA||op==0xB3||op==0xBB) return 0;   /* repeating: loop adds */
    if ((op>=0xA0&&op<=0xA3)||(op>=0xA8&&op<=0xAB)) return 16; /* one-shot block */
    if ((op&0xC7)==0x40) return 12;   /* IN r,(C)  */
    if ((op&0xC7)==0x41) return 12;   /* OUT (C),r */
    if ((op&0xCF)==0x42) return 15;   /* SBC HL,rp */
    if ((op&0xCF)==0x4A) return 15;   /* ADC HL,rp */
    if ((op&0xCF)==0x43) return 20;   /* LD (nn),rp */
    if ((op&0xCF)==0x4B) return 20;   /* LD rp,(nn) */
    if ((op&0xC7)==0x44) return 8;    /* NEG */
    if ((op&0xC7)==0x45) return 14;   /* RETN/RETI */
    if ((op&0xC7)==0x46) return 8;    /* IM */
    if (op==0x47||op==0x4F||op==0x57||op==0x5F) return 9;   /* LD I/R/A,* */
    if (op==0x67||op==0x6F) return 18; /* RRD/RLD */
    return 8;
}

static int cyc_base(const Z80Insn *in){
    switch (in->prefix){
        case Z80_PFX_CB: { int z=in->opcode&7,x=in->opcode>>6; return (z==6)?(x==1?12:15):8; }
        case Z80_PFX_DDCB: case Z80_PFX_FDCB: return (in->opcode>>6)==1 ? 20 : 23;
        case Z80_PFX_DD: case Z80_PFX_FD:
            /* LD (IX/IY+d),n (opcode 36) is 19 T-states, not base(10)+4+8=22:
             * the immediate-store form's write phase is not extended the way
             * the other indexed ops are, so the generic +8 displacement penalty
             * over-charges it by 3. Every other DD/FD+disp op (ld r,(ix+d),
             * ld (ix+d),r, ALU (ix+d)=19; inc/dec (ix+d)=23) is exact. */
            if (in->opcode == 0x36) return 19;
            return base_cyc[in->opcode] + 4 + (in->uses_disp ? 8 : 0);
        case Z80_PFX_ED: return ed_cyc(in->opcode);
        default: return base_cyc[in->opcode];
    }
}

/* ---- target resolution ---- */
static void emit_alu(FILE *o, int y, const char *V){
    switch (y){
        case 0: fprintf(o,"        s->a = z80_add8(s, s->a, %s, 0);\n", V); break;
        case 1: fprintf(o,"        s->a = z80_add8(s, s->a, %s, z80f_c(s));\n", V); break;
        case 2: fprintf(o,"        s->a = z80_sub8(s, s->a, %s, 0);\n", V); break;
        case 3: fprintf(o,"        s->a = z80_sub8(s, s->a, %s, z80f_c(s));\n", V); break;
        case 4: fprintf(o,"        s->a = z80_and8(s, s->a, %s);\n", V); break;
        case 5: fprintf(o,"        s->a = z80_xor8(s, s->a, %s);\n", V); break;
        case 6: fprintf(o,"        s->a = z80_or8(s, s->a, %s);\n", V); break;
        default:fprintf(o,"        z80_cp8(s, s->a, %s);\n", V); break;
    }
}

/* ---- unprefixed + DD/FD plain group ---- */
static void emit_plain(FILE *o, const Z80Insn *in, const OpCtx *c){
    uint8_t op = in->opcode;
    int x=op>>6, y=(op>>3)&7, z=op&7, p=(op>>4)&3, q=(op>>3)&1;

    if (uses_hlmem(op)){
        if (c->idx) fprintf(o,"        uint16_t ea = (uint16_t)(%s + (%d));\n", c->idx, c->disp);
        else        fprintf(o,"        uint16_t ea = z80_hl(s);\n");
    }

    if (x==0){
        if (z==0){
            if (y==0) fprintf(o,"        ; /* nop */\n");
            else if (y==1) fprintf(o,"        { uint8_t t; t=s->a; s->a=s->a_; s->a_=t; t=s->f; s->f=s->f_; s->f_=t; }\n");
            else cg_fail(in);   /* y 2..7 are control flow */
            return;
        }
        if (z==1){
            if (q==0){ char v[32]; snprintf(v,sizeof v,"0x%04X", in->imm);
                       fprintf(o,"        %s\n", rpw(c,p,v)); }
            else { char v[160]; snprintf(v,sizeof v,"z80_add16(s, %s, %s)",
                       c->idx ? c->idx : "z80_hl(s)", rpr(c,p));
                   fprintf(o,"        %s\n", rpw(c,2,v)); }
            return;
        }
        if (z==2){
            if (q==0){
                if (p==0) fprintf(o,"        sms_write8(z80_bc(s), s->a);\n");
                else if (p==1) fprintf(o,"        sms_write8(z80_de(s), s->a);\n");
                else if (p==2) fprintf(o,"        sms_write16(0x%04X, %s);\n", in->imm, c->idx ? c->idx : "z80_hl(s)");
                else fprintf(o,"        sms_write8(0x%04X, s->a);\n", in->imm);
            } else {
                if (p==0) fprintf(o,"        s->a = sms_read8(z80_bc(s));\n");
                else if (p==1) fprintf(o,"        s->a = sms_read8(z80_de(s));\n");
                else if (p==2){ char v[40]; snprintf(v,sizeof v,"sms_read16(0x%04X)", in->imm);
                                fprintf(o,"        %s\n", rpw(c,2,v)); }
                else fprintf(o,"        s->a = sms_read8(0x%04X);\n", in->imm);
            }
            return;
        }
        if (z==3){ char v[160]; snprintf(v,sizeof v,"(uint16_t)(%s %s 1)", rpr(c,p), q==0?"+":"-");
                   fprintf(o,"        %s\n", rpw(c,p,v)); return; }
        if (z==4){ char v[160]; snprintf(v,sizeof v,"z80_inc8(s, %s)", r8r(c,y));
                   fprintf(o,"        %s\n", r8w(c,y,v)); return; }
        if (z==5){ char v[160]; snprintf(v,sizeof v,"z80_dec8(s, %s)", r8r(c,y));
                   fprintf(o,"        %s\n", r8w(c,y,v)); return; }
        if (z==6){ char v[32]; snprintf(v,sizeof v,"0x%02X", (uint8_t)in->imm);
                   fprintf(o,"        %s\n", r8w(c,y,v)); return; }
        /* z==7 */{
            static const char *R[8] = {"z80_rlca","z80_rrca","z80_rla","z80_rra",
                                       "z80_daa","z80_cpl","z80_scf","z80_ccf"};
            fprintf(o,"        %s(s);\n", R[y]); return;
        }
    }

    if (x==1){   /* LD r,r' (HALT 0x76 handled by caller) */
        char v[160]; snprintf(v,sizeof v,"%s", r8r(c,z));
        fprintf(o,"        %s\n", r8w(c,y,v)); return;
    }

    if (x==2){   /* ALU A,r */
        char v[160]; snprintf(v,sizeof v,"%s", r8r(c,z));
        emit_alu(o,y,v); return;
    }

    /* x==3 */
    if (z==0) cg_fail(in);                  /* RET cc (control flow) */
    if (z==1){
        if (q==0){ char v[40]; snprintf(v,sizeof v,"sms_read16(s->sp)");
                   fprintf(o,"        %s s->sp = (uint16_t)(s->sp + 2);\n", rp2w(c,p,v)); return; }
        if (p==1){ fprintf(o,"        { uint8_t t;"
                     " t=s->b;s->b=s->b_;s->b_=t; t=s->c;s->c=s->c_;s->c_=t;"
                     " t=s->d;s->d=s->d_;s->d_=t; t=s->e;s->e=s->e_;s->e_=t;"
                     " t=s->h;s->h=s->h_;s->h_=t; t=s->l;s->l=s->l_;s->l_=t; }\n"); return; }
        if (p==3){ fprintf(o,"        s->sp = %s;\n", c->idx ? c->idx : "z80_hl(s)"); return; }
        cg_fail(in);                        /* p0 RET, p2 JP (HL): control flow */
    }
    if (z==2) cg_fail(in);                  /* JP cc */
    if (z==3){
        if (y==0 || y==1) cg_fail(in);      /* JP nn / CB prefix */
        if (y==2){ fprintf(o,"        sms_io_out(0x%02X, s->a);\n", (uint8_t)in->imm); return; }
        if (y==3){ fprintf(o,"        s->a = sms_io_in(0x%02X);\n", (uint8_t)in->imm); return; }
        if (y==4){ const char *hl = c->idx ? c->idx : "z80_hl(s)";
                   fprintf(o,"        { uint16_t t = sms_read16(s->sp); sms_write16(s->sp, %s); %s s->wz = t; }\n",
                           hl, rpw(c,2,"t")); return; }
        if (y==5){ fprintf(o,"        { uint8_t t; t=s->d;s->d=s->h;s->h=t; t=s->e;s->e=s->l;s->l=t; }\n"); return; }
        if (y==6){ fprintf(o,"        s->iff1 = s->iff2 = false;\n"); return; }
        fprintf(o,"        s->iff1 = s->iff2 = true; s->ei_block = 1;\n"); return;   /* y==7 EI (1-insn IRQ delay) */
    }
    if (z==4) cg_fail(in);                  /* CALL cc */
    if (z==5){
        if (q==0){ fprintf(o,"        s->sp = (uint16_t)(s->sp - 2); sms_write16(s->sp, %s);\n", rp2r(c,p)); return; }
        cg_fail(in);                        /* CALL nn / DD/ED/FD prefix */
    }
    if (z==6){ char v[32]; snprintf(v,sizeof v,"0x%02X", (uint8_t)in->imm); emit_alu(o,y,v); return; }
    cg_fail(in);                            /* z==7 RST: control flow */
}

/* ---- CB group ---- */
static void emit_cb(FILE *o, const Z80Insn *in){
    uint8_t op=in->opcode; int x=op>>6, y=(op>>3)&7, z=op&7;
    OpCtx c = {NULL,false,0};
    if (z==6) fprintf(o,"        uint16_t ea = z80_hl(s);\n");
    static const char *R[8] = {"z80_rlc","z80_rrc","z80_rl","z80_rr","z80_sla","z80_sra","z80_sll","z80_srl"};
    if (x==0){ char v[160]; snprintf(v,sizeof v,"%s(s, %s)", R[y], r8r(&c,z));
               fprintf(o,"        %s\n", r8w(&c,z,v)); }
    else if (x==1){
        if (z==6) fprintf(o,"        z80_bit(s, %d, sms_read8(ea), (uint8_t)(z80_hl(s) >> 8));\n", y);
        else      fprintf(o,"        z80_bit(s, %d, %s, %s);\n", y, RN8[z], RN8[z]);
    }
    else if (x==2){ char v[160]; snprintf(v,sizeof v,"(uint8_t)(%s & ~(1u << %d))", r8r(&c,z), y);
                    fprintf(o,"        %s\n", r8w(&c,z,v)); }
    else { char v[160]; snprintf(v,sizeof v,"(uint8_t)(%s | (1u << %d))", r8r(&c,z), y);
           fprintf(o,"        %s\n", r8w(&c,z,v)); }
}

/* ---- DDCB / FDCB group: op on (IX+d)/(IY+d), undocumented register copy ---- */
static void emit_ddcb(FILE *o, const Z80Insn *in, const char *idx){
    uint8_t op=in->opcode; int x=op>>6, y=(op>>3)&7, z=op&7;
    fprintf(o,"        uint16_t ea = (uint16_t)(%s + (%d));\n", idx, in->disp);
    fprintf(o,"        uint8_t v = sms_read8(ea);\n");
    if (x==1){ fprintf(o,"        z80_bit(s, %d, v, (uint8_t)(ea >> 8));\n", y); return; }
    static const char *R[8] = {"z80_rlc","z80_rrc","z80_rl","z80_rr","z80_sla","z80_sra","z80_sll","z80_srl"};
    if (x==0)      fprintf(o,"        uint8_t r = %s(s, v);\n", R[y]);
    else if (x==2) fprintf(o,"        uint8_t r = (uint8_t)(v & ~(1u << %d));\n", y);
    else           fprintf(o,"        uint8_t r = (uint8_t)(v | (1u << %d));\n", y);
    fprintf(o,"        sms_write8(ea, r);\n");
    if (z!=6) fprintf(o,"        %s = r;\n", RN8[z]);   /* undoc: also store to reg z */
}

/* ---- ED block ops (LDI..OTDR), one body emitter for all 16 ---- */
static void emit_block_body(FILE *o, uint8_t op, const char *ind){
    int grp = op & 3;             /* 0 LD, 1 CP, 2 IN, 3 OUT */
    bool dec = (op & 0x08) != 0;
    const char *d = dec ? "- 1" : "+ 1";
    if (grp==0){
        fprintf(o,"%s{ uint8_t v = sms_read8(z80_hl(s)); sms_write8(z80_de(s), v);\n", ind);
        fprintf(o,"%s  z80_set_de(s, (uint16_t)(z80_de(s) %s)); z80_set_hl(s, (uint16_t)(z80_hl(s) %s));\n", ind, d, d);
        fprintf(o,"%s  z80_set_bc(s, (uint16_t)(z80_bc(s) - 1));\n", ind);
        fprintf(o,"%s  uint8_t n = (uint8_t)(s->a + v);\n", ind);
        fprintf(o,"%s  s->f = (uint8_t)((s->f & (Z80_FLAG_C|Z80_FLAG_Z|Z80_FLAG_S)) | (z80_bc(s)!=0?Z80_FLAG_P:0) | (n & 0x08) | ((n & 0x02)?Z80_FLAG_Y:0)); }\n", ind);
    } else if (grp==1){
        fprintf(o,"%s{ uint8_t v = sms_read8(z80_hl(s)); uint8_t r = (uint8_t)(s->a - v);\n", ind);
        fprintf(o,"%s  z80_set_hl(s, (uint16_t)(z80_hl(s) %s)); z80_set_bc(s, (uint16_t)(z80_bc(s) - 1));\n", ind, d);
        fprintf(o,"%s  uint8_t hc = ((s->a & 0x0F) < (v & 0x0F)) ? 1u : 0u;\n", ind);
        fprintf(o,"%s  uint8_t n = (uint8_t)(r - hc);\n", ind);
        fprintf(o,"%s  s->f = (uint8_t)((s->f & Z80_FLAG_C) | Z80_FLAG_N | (r & 0x80) | (r==0?Z80_FLAG_Z:0) | (hc?Z80_FLAG_H:0) | (z80_bc(s)!=0?Z80_FLAG_P:0) | (n & 0x08) | ((n & 0x02)?Z80_FLAG_Y:0)); }\n", ind);
    } else if (grp==2){
        const char *cd = dec ? "- 1" : "+ 1";
        fprintf(o,"%s{ uint8_t v = sms_io_in(s->c); sms_write8(z80_hl(s), v);\n", ind);
        fprintf(o,"%s  s->b = (uint8_t)(s->b - 1); z80_set_hl(s, (uint16_t)(z80_hl(s) %s));\n", ind, d);
        fprintf(o,"%s  uint16_t k = (uint16_t)v + (uint16_t)((uint8_t)(s->c %s));\n", ind, cd);
        fprintf(o,"%s  s->f = (uint8_t)(z80_szxy(s->b) | (v & 0x80 ? Z80_FLAG_N:0) | (k > 0xFF ? (Z80_FLAG_H|Z80_FLAG_C):0) | (z80_parity8((uint8_t)((k & 7) ^ s->b)) ? Z80_FLAG_P:0)); }\n", ind);
    } else {
        fprintf(o,"%s{ uint8_t v = sms_read8(z80_hl(s)); s->b = (uint8_t)(s->b - 1);\n", ind);
        fprintf(o,"%s  sms_io_out(s->c, v); z80_set_hl(s, (uint16_t)(z80_hl(s) %s));\n", ind, d);
        fprintf(o,"%s  uint16_t k = (uint16_t)v + (uint16_t)s->l;\n", ind);
        fprintf(o,"%s  s->f = (uint8_t)(z80_szxy(s->b) | (v & 0x80 ? Z80_FLAG_N:0) | (k > 0xFF ? (Z80_FLAG_H|Z80_FLAG_C):0) | (z80_parity8((uint8_t)((k & 7) ^ s->b)) ? Z80_FLAG_P:0)); }\n", ind);
    }
}
static void emit_block(FILE *o, uint8_t op){
    int grp = op & 3; bool rep = (op & 0x10) != 0;
    if (!rep){ emit_block_body(o, op, "        "); return; }
    const char *contc = (grp==0) ? "z80_bc(s) != 0"
                      : (grp==1) ? "z80_bc(s) != 0 && !(s->f & Z80_FLAG_Z)"
                                 : "s->b != 0";
    /* Repeating block ops are INTERRUPTIBLE per iteration on real Z80: the CPU
     * runs one iteration, accounts its cycles, and accepts a pending interrupt
     * between iterations (re-executing the opcode afterwards). Tick per iteration
     * (21 cyc continuing / 16 last) so the VDP advances and IRQs are sampled
     * mid-op exactly like the interpreter — otherwise a long LDIR/OTIR with
     * interrupts enabled defers every VBlank to after it and the timeline
     * diverges from the oracle. */
    fprintf(o,"        do {\n");
    emit_block_body(o, op, "            ");
    /* post-body tick: this iteration's effect already ran (see sms_tick_post) */
    fprintf(o,"            sms_tick_post((%s) ? 21 : 16);\n", contc);
    fprintf(o,"        } while (%s);\n", contc);
}

/* ---- ED group ---- */
static void emit_ed(FILE *o, const Z80Insn *in){
    uint8_t op = in->opcode;
    OpCtx c0 = {NULL,false,0};
    /* IN r,(C) / IN (C) */
    if ((op&0xC7)==0x40){ int r=(op>>3)&7;
        fprintf(o,"        { uint8_t v = sms_io_in(s->c); ");
        if (r!=6) fprintf(o,"%s = v; ", RN8[r]);
        fprintf(o,"s->f = (uint8_t)((s->f & Z80_FLAG_C) | z80_szxy(v) | (z80_parity8(v)?Z80_FLAG_P:0)); }\n");
        return; }
    /* OUT (C),r / OUT (C),0 */
    if ((op&0xC7)==0x41){ int r=(op>>3)&7;
        fprintf(o,"        sms_io_out(s->c, %s);\n", r==6 ? "0" : RN8[r]); return; }
    /* SBC HL,rp */
    if ((op&0xCF)==0x42){ int p=(op>>4)&3;
        fprintf(o,"        z80_set_hl(s, z80_sbc16(s, z80_hl(s), %s));\n", rpr(&c0,p)); return; }
    /* ADC HL,rp */
    if ((op&0xCF)==0x4A){ int p=(op>>4)&3;
        fprintf(o,"        z80_set_hl(s, z80_adc16(s, z80_hl(s), %s));\n", rpr(&c0,p)); return; }
    /* LD (nn),rp */
    if ((op&0xCF)==0x43){ int p=(op>>4)&3;
        fprintf(o,"        sms_write16(0x%04X, %s);\n", in->imm, rpr(&c0,p)); return; }
    /* LD rp,(nn) */
    if ((op&0xCF)==0x4B){ int p=(op>>4)&3; char v[40]; snprintf(v,sizeof v,"sms_read16(0x%04X)", in->imm);
        fprintf(o,"        %s\n", rpw(&c0,p,v)); return; }
    /* NEG */
    if ((op&0xC7)==0x44){ fprintf(o,"        z80_neg(s);\n"); return; }
    /* IM */
    if ((op&0xC7)==0x46){ int m=(op>>3)&3; int im = (m==2)?1 : (m==3)?2 : 0;
        fprintf(o,"        s->im = %d;\n", im); return; }
    switch (op){
        case 0x47: fprintf(o,"        s->i = s->a;\n"); return;
        case 0x4F: fprintf(o,"        s->r = s->a;\n"); return;
        case 0x57: fprintf(o,"        s->a = s->i; s->f = (uint8_t)((s->f & Z80_FLAG_C) | z80_szxy(s->a) | (s->iff2?Z80_FLAG_P:0));\n"); return;
        case 0x5F: fprintf(o,"        s->a = s->r; s->f = (uint8_t)((s->f & Z80_FLAG_C) | z80_szxy(s->a) | (s->iff2?Z80_FLAG_P:0));\n"); return;
        case 0x67: /* RRD */
            fprintf(o,"        { uint8_t m = sms_read8(z80_hl(s)); uint8_t a = s->a;\n");
            fprintf(o,"          sms_write8(z80_hl(s), (uint8_t)((m >> 4) | (a << 4)));\n");
            fprintf(o,"          s->a = (uint8_t)((a & 0xF0) | (m & 0x0F));\n");
            fprintf(o,"          s->f = (uint8_t)((s->f & Z80_FLAG_C) | z80_szxy(s->a) | (z80_parity8(s->a)?Z80_FLAG_P:0)); }\n");
            return;
        case 0x6F: /* RLD */
            fprintf(o,"        { uint8_t m = sms_read8(z80_hl(s)); uint8_t a = s->a;\n");
            fprintf(o,"          sms_write8(z80_hl(s), (uint8_t)((m << 4) | (a & 0x0F)));\n");
            fprintf(o,"          s->a = (uint8_t)((a & 0xF0) | (m >> 4));\n");
            fprintf(o,"          s->f = (uint8_t)((s->f & Z80_FLAG_C) | z80_szxy(s->a) | (z80_parity8(s->a)?Z80_FLAG_P:0)); }\n");
            return;
        default: break;
    }
    if ((op>=0xA0&&op<=0xA3)||(op>=0xA8&&op<=0xAB)||
        (op>=0xB0&&op<=0xB3)||(op>=0xB8&&op<=0xBB)){
        emit_block(o, op); return;
    }
    /* Every remaining ED encoding is an architectural 2-byte no-op. */
    fprintf(o,"        ; /* undocumented ED no-op */\n");
}

/* ---- one non-control instruction ---- */
static void emit_op(FILE *o, const Z80Insn *in){
    if (in->is_halt){ fprintf(o,"        sms_halt();\n"); return; }
    switch (in->prefix){
        case Z80_PFX_CB:   emit_cb(o,in); break;
        case Z80_PFX_DDCB: emit_ddcb(o,in,"s->ix"); break;
        case Z80_PFX_FDCB: emit_ddcb(o,in,"s->iy"); break;
        case Z80_PFX_ED:   emit_ed(o,in); break;
        case Z80_PFX_DD:   { OpCtx c={"s->ix", in->uses_disp, in->disp}; emit_plain(o,in,&c); break; }
        case Z80_PFX_FD:   { OpCtx c={"s->iy", in->uses_disp, in->disp}; emit_plain(o,in,&c); break; }
        default:           { OpCtx c={NULL,false,0}; emit_plain(o,in,&c); break; }
    }
}

/* ---- jump target (goto for intra-function, tail call otherwise) ---- */
static bool flat_is_repeat_block(const Z80Insn *in)
{
    if (in->prefix != Z80_PFX_ED) return false;
    uint8_t op = in->opcode;
    return (op >= 0xB0 && op <= 0xB3) || (op >= 0xB8 && op <= 0xBB);
}

static void emit_flat_repeat(FILE *o, const Z80Insn *in, uint16_t addr,
                             uint16_t next)
{
    int grp = in->opcode & 3;
    const char *contc = (grp == 0) ? "z80_bc(s) != 0"
                      : (grp == 1) ? "z80_bc(s) != 0 && !(s->f & Z80_FLAG_Z)"
                                   : "s->b != 0";
    emit_block_body(o, in->opcode, "        ");
    fprintf(o, "        if (%s) { s->cyc += 21; s->pc = 0x%04X; }\n",
            contc, addr);
    fprintf(o, "        else { s->cyc += 16; s->pc = 0x%04X; }\n", next);
}

static void emit_flat_cf(FILE *o, const Z80Insn *in, uint16_t next)
{
    switch (in->cf) {
    case Z80_CF_RET:
        if (in->prefix == Z80_PFX_ED)
            fprintf(o, "        s->iff1 = s->iff2;\n");
        fprintf(o, "        s->pc = sms_read16(s->sp);\n");
        fprintf(o, "        s->sp = (uint16_t)(s->sp + 2);\n");
        break;

    case Z80_CF_RET_COND: {
        int cc = (in->opcode >> 3) & 7;
        fprintf(o, "        if (%s) { s->cyc += 6; s->pc = sms_read16(s->sp); s->sp = (uint16_t)(s->sp + 2); }\n",
                cond(cc));
        break;
    }

    case Z80_CF_JUMP:
        if (in->has_target) {
            fprintf(o, "        s->pc = 0x%04X;\n", in->target);
        } else {
            const char *idx = (in->prefix == Z80_PFX_DD) ? "s->ix"
                            : (in->prefix == Z80_PFX_FD) ? "s->iy"
                                                        : "z80_hl(s)";
            fprintf(o, "        s->pc = %s;\n", idx);
        }
        break;

    case Z80_CF_JUMP_COND:
        if (in->prefix == Z80_PFX_NONE && in->opcode == 0x10) {
            fprintf(o, "        s->b = (uint8_t)(s->b - 1);\n");
            fprintf(o, "        if (s->b != 0) { s->cyc += 5; s->pc = 0x%04X; }\n",
                    in->target);
        } else if (in->prefix == Z80_PFX_NONE && (in->opcode & 0xE7) == 0x20) {
            fprintf(o, "        if (%s) { s->cyc += 5; s->pc = 0x%04X; }\n",
                    cond((in->opcode >> 3) & 3), in->target);
        } else {
            fprintf(o, "        if (%s) s->pc = 0x%04X;\n",
                    cond((in->opcode >> 3) & 7), in->target);
        }
        break;

    case Z80_CF_CALL:
        fprintf(o, "        s->sp = (uint16_t)(s->sp - 2); sms_write16(s->sp, 0x%04X); s->pc = 0x%04X;\n",
                next, in->target);
        break;

    case Z80_CF_CALL_COND: {
        int cc = (in->opcode >> 3) & 7;
        fprintf(o, "        if (%s) { s->cyc += 7; s->sp = (uint16_t)(s->sp - 2); sms_write16(s->sp, 0x%04X); s->pc = 0x%04X; }\n",
                cond(cc), next, in->target);
        break;
    }

    default:
        break;
    }
}

static void emit_flat_body(FILE *o, const Z80Insn *in, uint16_t addr)
{
    uint16_t next = (uint16_t)(addr + in->length);
    fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + %d) & 0x7f));\n",
            in->prefix == Z80_PFX_NONE ? 1 : 2);
    fprintf(o, "        s->pc = 0x%04X;\n", next);

    if (flat_is_repeat_block(in)) {
        emit_flat_repeat(o, in, addr, next);
    } else {
        int base = cyc_base(in);
        if (base > 0) fprintf(o, "        s->cyc += %d;\n", base);
        if (in->is_halt) {
            fprintf(o, "        s->halted = true;\n");
        } else if (in->cf == Z80_CF_NONE) {
            emit_op(o, in);
        } else {
            emit_flat_cf(o, in, next);
        }
    }
}


static bool emit_flat_live_operands(FILE *o, const Z80Insn *in, uint16_t addr)
{
    if (in->prefix == Z80_PFX_NONE && in->length == 3 &&
        (in->opcode & 0xC7) == 0xC2) {
        fprintf(o, "        if (sms_read8(0x%04X) == 0x%02X) {\n", addr, in->opcode);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 1) & 0x7f));\n");
        fprintf(o, "        s->pc = 0x%04X;\n", (uint16_t)(addr + 3));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        fprintf(o, "        uint16_t target = sms_read16(0x%04X);\n", (uint16_t)(addr + 1));
        fprintf(o, "        if (%s) s->pc = target;\n", cond((in->opcode >> 3) & 7));
        fprintf(o, "            return;\n        }\n");
        return true;
    }
    if (in->prefix == Z80_PFX_NONE && in->length == 3 && in->opcode == 0xC3) {
        fprintf(o, "        if (sms_read8(0x%04X) == 0xC3) {\n", addr);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 1) & 0x7f));\n");
        fprintf(o, "        s->pc = sms_read16(0x%04X);\n", (uint16_t)(addr + 1));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        fprintf(o, "            return;\n        }\n");
        return true;
    }
    if (in->prefix == Z80_PFX_NONE && in->length == 3 &&
        (in->opcode == 0x22 || in->opcode == 0x2A ||
         in->opcode == 0x32 || in->opcode == 0x3A)) {
        fprintf(o, "        if (sms_read8(0x%04X) == 0x%02X) {\n", addr,
                in->opcode);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 1) & 0x7f));\n");
        fprintf(o, "        s->pc = 0x%04X;\n", (uint16_t)(addr + 3));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        fprintf(o, "        uint16_t nn = sms_read16(0x%04X);\n",
                (uint16_t)(addr + 1));
        switch (in->opcode) {
        case 0x22: fprintf(o, "        sms_write16(nn, z80_hl(s));\n"); break;
        case 0x2A: fprintf(o, "        z80_set_hl(s, sms_read16(nn));\n"); break;
        case 0x32: fprintf(o, "        sms_write8(nn, s->a);\n"); break;
        case 0x3A: fprintf(o, "        s->a = sms_read8(nn);\n"); break;
        }
        fprintf(o, "            return;\n        }\n");
        return true;
    }

    if (in->prefix == Z80_PFX_NONE && in->length == 2 &&
        (in->opcode & 0xC7) == 0xC6) {
        char value[40];
        snprintf(value, sizeof value, "sms_read8(0x%04X)", (uint16_t)(addr + 1));
        fprintf(o, "        if (sms_read8(0x%04X) == 0x%02X) {\n", addr, in->opcode);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 1) & 0x7f));\n");
        fprintf(o, "        s->pc = 0x%04X;\n", (uint16_t)(addr + 2));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        emit_alu(o, (in->opcode >> 3) & 7, value);
        fprintf(o, "            return;\n        }\n");
        return true;
    }

    if (in->prefix == Z80_PFX_NONE && (in->opcode & 0xC7) == 0x06 &&
        in->length == 2) {
        int y = (in->opcode >> 3) & 7;
        OpCtx c = {NULL, false, 0};
        char value[40];
        snprintf(value, sizeof value, "sms_read8(0x%04X)", (uint16_t)(addr + 1));
        fprintf(o, "        if (sms_read8(0x%04X) == 0x%02X) {\n", addr,
                in->opcode);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 1) & 0x7f));\n");
        fprintf(o, "        s->pc = 0x%04X;\n", (uint16_t)(addr + 2));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        if (y == 6) fprintf(o, "        uint16_t ea = z80_hl(s);\n");
        fprintf(o, "        %s\n", r8w(&c, y, value));
        fprintf(o, "            return;\n        }\n");
        return true;
    }

    if ((in->prefix == Z80_PFX_DD || in->prefix == Z80_PFX_FD) &&
        in->uses_disp && in->length == 3 &&
        (in->opcode >> 6) == 2 && (in->opcode & 7) == 6) {
        const char *idx = in->prefix == Z80_PFX_DD ? "s->ix" : "s->iy";
        int alu = (in->opcode >> 3) & 7;
        fprintf(o, "        if (sms_read8(0x%04X) == 0x%02X && sms_read8(0x%04X) == 0x%02X) {\n",
                addr, in->raw[0], (uint16_t)(addr + 1), in->opcode);
        fprintf(o, "        s->r = (uint8_t)((s->r & 0x80) | ((s->r + 2) & 0x7f));\n");
        fprintf(o, "        s->pc = 0x%04X;\n", (uint16_t)(addr + 3));
        fprintf(o, "        s->cyc += %d;\n", cyc_base(in));
        fprintf(o, "        uint16_t ea = (uint16_t)(%s + (int8_t)sms_read8(0x%04X));\n",
                idx, (uint16_t)(addr + 2));
        emit_alu(o, alu, "sms_read8(ea)");
        fprintf(o, "            return;\n        }\n");
        return true;
    }
    return false;
}

static Z80EmitResult emit_transaction(FILE *output,FILE *scratch,
 const Z80Insn *in,uint16_t address,int live) {
 if(!output||!scratch||output==scratch||!in||in->length<1||in->length>4||
    (unsigned)in->prefix>Z80_PFX_FDCB)return Z80_EMIT_INVALID;
 if(!live&&in->illegal)return Z80_EMIT_UNSUPPORTED;
 clearerr(scratch);
 if(fseek(scratch,0,SEEK_SET))return Z80_EMIT_IO;
 if(setjmp(emitter_escape))return Z80_EMIT_UNSUPPORTED;
 if(live){if(!emit_flat_live_operands(scratch,in,address))return Z80_EMIT_UNSUPPORTED;}
 else emit_flat_body(scratch,in,address);
 if(ferror(scratch)||fflush(scratch))return Z80_EMIT_IO;
 const long length=ftell(scratch);
 if(length<0||fseek(scratch,0,SEEK_SET))return Z80_EMIT_IO;
 char block[4096];size_t remaining=(size_t)length;
 while(remaining){
  const size_t wanted=remaining<sizeof block?remaining:sizeof block;
  const size_t count=fread(block,1,wanted,scratch);
  if(count!=wanted||fwrite(block,1,count,output)!=count)return Z80_EMIT_IO;
  remaining-=count;
 }
 return Z80_EMIT_OK;
}
Z80EmitResult z80_emit_step_body_with_scratch(FILE *output,FILE *scratch,const Z80Insn *in,uint16_t address) {
 return emit_transaction(output,scratch,in,address,0);
}
Z80EmitResult z80_emit_live_operand_case_with_scratch(FILE *output,FILE *scratch,const Z80Insn *in,uint16_t address) {
 return emit_transaction(output,scratch,in,address,1);
}
Z80EmitResult z80_emit_step_body(FILE *output,const Z80Insn *in,uint16_t address) {
 if(!output||!in||in->length<1||in->length>4||(unsigned)in->prefix>Z80_PFX_FDCB)return Z80_EMIT_INVALID;
 if(in->illegal)return Z80_EMIT_UNSUPPORTED;
 FILE *scratch=tmpfile();if(!scratch)return Z80_EMIT_IO;
 Z80EmitResult result=z80_emit_step_body_with_scratch(output,scratch,in,address);
 fclose(scratch);return result;
}
Z80EmitResult z80_emit_live_operand_case(FILE *output,const Z80Insn *in,uint16_t address) {
 if(!output||!in||in->length<1||in->length>4||(unsigned)in->prefix>Z80_PFX_FDCB)return Z80_EMIT_INVALID;
 FILE *scratch=tmpfile();if(!scratch)return Z80_EMIT_IO;
 Z80EmitResult result=z80_emit_live_operand_case_with_scratch(output,scratch,in,address);
 fclose(scratch);return result;
}
