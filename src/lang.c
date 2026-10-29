#include "lang.h"

#include <string.h>

#include "Lexilla.h"
#include "SciLexer.h"
#include "settings.h"

enum { B = 1, I = 2 };

typedef struct { int style; Role role; int flags; } StyleDef;

typedef struct {
    const char *name;     /* shown in the status bar */
    const char *lexer;    /* Lexilla lexer name */
    const char *exts;     /* ";ext;ext;" lower case */
    const char *files;    /* ";exact-name;" */
    const char *kw0, *kw1;
    const StyleDef *styles;
    size_t nstyles;
    const char *kw2, *kw3;   /* extra keyword sets (asm: registers, directives) */
} Lang;

static const StyleDef cpp_styles[] = {
    { SCE_C_COMMENT, R_COMMENT, I }, { SCE_C_COMMENTLINE, R_COMMENT, I },
    { SCE_C_COMMENTDOC, R_COMMENT, I }, { SCE_C_COMMENTLINEDOC, R_COMMENT, I },
    { SCE_C_NUMBER, R_NUMBER, 0 }, { SCE_C_WORD, R_KEYWORD, B },
    { SCE_C_WORD2, R_TYPE, 0 }, { SCE_C_STRING, R_STRING, 0 },
    { SCE_C_CHARACTER, R_STRING, 0 }, { SCE_C_STRINGRAW, R_STRING, 0 },
    { SCE_C_VERBATIM, R_STRING, 0 }, { SCE_C_REGEX, R_STRING, 0 },
    { SCE_C_PREPROCESSOR, R_PREPROC, 0 },
};
static const StyleDef py_styles[] = {
    { SCE_P_COMMENTLINE, R_COMMENT, I }, { SCE_P_COMMENTBLOCK, R_COMMENT, I },
    { SCE_P_NUMBER, R_NUMBER, 0 }, { SCE_P_STRING, R_STRING, 0 },
    { SCE_P_CHARACTER, R_STRING, 0 }, { SCE_P_TRIPLE, R_STRING, 0 },
    { SCE_P_TRIPLEDOUBLE, R_STRING, 0 }, { SCE_P_FSTRING, R_STRING, 0 },
    { SCE_P_FCHARACTER, R_STRING, 0 }, { SCE_P_FTRIPLE, R_STRING, 0 },
    { SCE_P_FTRIPLEDOUBLE, R_STRING, 0 }, { SCE_P_WORD, R_KEYWORD, B },
    { SCE_P_WORD2, R_TYPE, 0 }, { SCE_P_DECORATOR, R_PREPROC, 0 },
    { SCE_P_CLASSNAME, R_TYPE, B }, { SCE_P_DEFNAME, R_TYPE, B },
};
static const StyleDef sh_styles[] = {
    { SCE_SH_COMMENTLINE, R_COMMENT, I }, { SCE_SH_NUMBER, R_NUMBER, 0 },
    { SCE_SH_WORD, R_KEYWORD, B }, { SCE_SH_STRING, R_STRING, 0 },
    { SCE_SH_CHARACTER, R_STRING, 0 }, { SCE_SH_BACKTICKS, R_PREPROC, 0 },
    { SCE_SH_PARAM, R_TYPE, 0 }, { SCE_SH_SCALAR, R_TYPE, 0 },
    { SCE_SH_HERE_DELIM, R_PREPROC, 0 }, { SCE_SH_HERE_Q, R_STRING, 0 },
};
static const StyleDef json_styles[] = {
    { SCE_JSON_NUMBER, R_NUMBER, 0 }, { SCE_JSON_STRING, R_STRING, 0 },
    { SCE_JSON_PROPERTYNAME, R_TYPE, 0 }, { SCE_JSON_KEYWORD, R_KEYWORD, B },
    { SCE_JSON_LINECOMMENT, R_COMMENT, I }, { SCE_JSON_BLOCKCOMMENT, R_COMMENT, I },
    { SCE_JSON_ESCAPESEQUENCE, R_PREPROC, 0 },
};
static const StyleDef html_styles[] = {
    { SCE_H_TAG, R_TAG, B }, { SCE_H_TAGUNKNOWN, R_TAG, 0 },
    { SCE_H_ATTRIBUTE, R_TYPE, 0 }, { SCE_H_ATTRIBUTEUNKNOWN, R_TYPE, 0 },
    { SCE_H_NUMBER, R_NUMBER, 0 }, { SCE_H_DOUBLESTRING, R_STRING, 0 },
    { SCE_H_SINGLESTRING, R_STRING, 0 }, { SCE_H_COMMENT, R_COMMENT, I },
    { SCE_H_ENTITY, R_PREPROC, 0 }, { SCE_H_XMLSTART, R_PREPROC, 0 },
    { SCE_H_XMLEND, R_PREPROC, 0 }, { SCE_H_CDATA, R_STRING, 0 },
};
static const StyleDef css_styles[] = {
    { SCE_CSS_TAG, R_TAG, B }, { SCE_CSS_CLASS, R_TYPE, 0 },
    { SCE_CSS_ID, R_TYPE, 0 }, { SCE_CSS_PSEUDOCLASS, R_TYPE, 0 },
    { SCE_CSS_IDENTIFIER, R_KEYWORD, 0 }, { SCE_CSS_VALUE, R_NUMBER, 0 },
    { SCE_CSS_COMMENT, R_COMMENT, I }, { SCE_CSS_DOUBLESTRING, R_STRING, 0 },
    { SCE_CSS_SINGLESTRING, R_STRING, 0 }, { SCE_CSS_IMPORTANT, R_KEYWORD, B },
    { SCE_CSS_DIRECTIVE, R_PREPROC, 0 },
};
static const StyleDef make_styles[] = {
    { SCE_MAKE_COMMENT, R_COMMENT, I }, { SCE_MAKE_PREPROCESSOR, R_PREPROC, 0 },
    { SCE_MAKE_IDENTIFIER, R_TYPE, 0 }, { SCE_MAKE_TARGET, R_KEYWORD, B },
};
static const StyleDef yaml_styles[] = {
    { SCE_YAML_COMMENT, R_COMMENT, I }, { SCE_YAML_IDENTIFIER, R_TYPE, 0 },
    { SCE_YAML_KEYWORD, R_KEYWORD, B }, { SCE_YAML_NUMBER, R_NUMBER, 0 },
    { SCE_YAML_REFERENCE, R_PREPROC, 0 }, { SCE_YAML_DOCUMENT, R_PREPROC, B },
    { SCE_YAML_TEXT, R_STRING, 0 },
};
static const StyleDef md_styles[] = {
    { SCE_MARKDOWN_HEADER1, R_KEYWORD, B }, { SCE_MARKDOWN_HEADER2, R_KEYWORD, B },
    { SCE_MARKDOWN_HEADER3, R_KEYWORD, B }, { SCE_MARKDOWN_HEADER4, R_KEYWORD, B },
    { SCE_MARKDOWN_HEADER5, R_KEYWORD, B }, { SCE_MARKDOWN_HEADER6, R_KEYWORD, B },
    { SCE_MARKDOWN_STRONG1, R_FG, B }, { SCE_MARKDOWN_STRONG2, R_FG, B },
    { SCE_MARKDOWN_EM1, R_FG, I }, { SCE_MARKDOWN_EM2, R_FG, I },
    { SCE_MARKDOWN_CODE, R_STRING, 0 }, { SCE_MARKDOWN_CODE2, R_STRING, 0 },
    { SCE_MARKDOWN_CODEBK, R_STRING, 0 }, { SCE_MARKDOWN_LINK, R_NUMBER, 0 },
    { SCE_MARKDOWN_BLOCKQUOTE, R_COMMENT, I }, { SCE_MARKDOWN_ULIST_ITEM, R_PREPROC, 0 },
    { SCE_MARKDOWN_OLIST_ITEM, R_PREPROC, 0 },
};
static const StyleDef props_styles[] = {
    { SCE_PROPS_COMMENT, R_COMMENT, I }, { SCE_PROPS_SECTION, R_KEYWORD, B },
    { SCE_PROPS_ASSIGNMENT, R_PREPROC, 0 }, { SCE_PROPS_KEY, R_TYPE, 0 },
    { SCE_PROPS_DEFVAL, R_NUMBER, 0 },
};
static const StyleDef diff_styles[] = {
    { SCE_DIFF_COMMENT, R_COMMENT, I }, { SCE_DIFF_COMMAND, R_KEYWORD, B },
    { SCE_DIFF_HEADER, R_TYPE, B }, { SCE_DIFF_POSITION, R_PREPROC, 0 },
    { SCE_DIFF_DELETED, R_DELETED, 0 }, { SCE_DIFF_ADDED, R_ADDED, 0 },
    { SCE_DIFF_CHANGED, R_NUMBER, 0 },
};
static const StyleDef rust_styles[] = {
    { SCE_RUST_COMMENTBLOCK, R_COMMENT, I }, { SCE_RUST_COMMENTLINE, R_COMMENT, I },
    { SCE_RUST_COMMENTBLOCKDOC, R_COMMENT, I }, { SCE_RUST_COMMENTLINEDOC, R_COMMENT, I },
    { SCE_RUST_NUMBER, R_NUMBER, 0 }, { SCE_RUST_WORD, R_KEYWORD, B },
    { SCE_RUST_WORD2, R_TYPE, 0 }, { SCE_RUST_STRING, R_STRING, 0 },
    { SCE_RUST_STRINGR, R_STRING, 0 }, { SCE_RUST_CHARACTER, R_STRING, 0 },
    { SCE_RUST_BYTESTRING, R_STRING, 0 }, { SCE_RUST_MACRO, R_PREPROC, 0 },
    { SCE_RUST_LIFETIME, R_TYPE, I },
};

static const StyleDef latex_styles[] = {
    { SCE_L_COMMAND, R_KEYWORD, B }, { SCE_L_TAG, R_TAG, B },
    { SCE_L_TAG2, R_TAG, B }, { SCE_L_MATH, R_NUMBER, 0 },
    { SCE_L_MATH2, R_NUMBER, 0 }, { SCE_L_COMMENT, R_COMMENT, I },
    { SCE_L_COMMENT2, R_COMMENT, I }, { SCE_L_VERBATIM, R_STRING, 0 },
    { SCE_L_SHORTCMD, R_PREPROC, 0 }, { SCE_L_SPECIAL, R_PREPROC, B },
    { SCE_L_CMDOPT, R_TYPE, 0 }, { SCE_L_ERROR, R_DELETED, B },
};
static const StyleDef asm_styles[] = {
    { SCE_ASM_COMMENT, R_COMMENT, I }, { SCE_ASM_COMMENTBLOCK, R_COMMENT, I },
    { SCE_ASM_COMMENTDIRECTIVE, R_COMMENT, I }, { SCE_ASM_NUMBER, R_NUMBER, 0 },
    { SCE_ASM_STRING, R_STRING, 0 }, { SCE_ASM_CHARACTER, R_STRING, 0 },
    { SCE_ASM_STRINGEOL, R_STRING, 0 }, { SCE_ASM_STRINGBACKQUOTE, R_STRING, 0 },
    { SCE_ASM_CPUINSTRUCTION, R_KEYWORD, B }, { SCE_ASM_MATHINSTRUCTION, R_KEYWORD, 0 },
    { SCE_ASM_EXTINSTRUCTION, R_KEYWORD, 0 }, { SCE_ASM_REGISTER, R_TYPE, 0 },
    { SCE_ASM_DIRECTIVE, R_PREPROC, B }, { SCE_ASM_DIRECTIVEOPERAND, R_PREPROC, 0 },
};

#define STYLES(a) a, G_N_ELEMENTS(a)

static const char kw_c[] =
    "auto break case const continue default do else enum extern for goto if "
    "inline register restrict return sizeof static struct switch typedef union "
    "volatile while _Alignas _Alignof _Atomic _Bool _Generic _Noreturn "
    "_Static_assert _Thread_local alignas alignof bool constexpr static_assert "
    "thread_local typeof true false NULL nullptr "
    "class namespace template typename public private protected virtual override "
    "new delete this try catch throw using operator friend explicit mutable "
    "noexcept final decltype consteval";
static const char kw_c_types[] =
    "char double float int long short signed unsigned void size_t ssize_t "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
    "intptr_t uintptr_t ptrdiff_t off_t FILE gboolean gchar gint guint gsize "
    "gpointer gconstpointer gint64 guint64 string vector";
static const char kw_js[] =
    "break case catch class const continue debugger default delete do else "
    "export extends finally for function if import in instanceof let new "
    "return super switch this throw try typeof var void while with yield "
    "async await of null undefined true false static get set";
static const char kw_go[] =
    "break case chan const continue default defer else fallthrough for func go "
    "goto if import interface map package range return select struct switch "
    "type var nil true false iota";
static const char kw_go_types[] =
    "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 "
    "int64 rune string uint uint8 uint16 uint32 uint64 uintptr any";
static const char kw_java[] =
    "abstract assert break case catch class const continue default do else "
    "enum extends final finally for goto if implements import instanceof "
    "interface native new package private protected public return static "
    "strictfp super switch synchronized this throw throws transient try "
    "volatile while var record true false null";
static const char kw_java_types[] =
    "boolean byte char double float int long short void String Object";
static const char kw_py[] =
    "False None True and as assert async await break class continue def del "
    "elif else except finally for from global if import in is lambda nonlocal "
    "not or pass raise return try while with yield match case";
static const char kw_py_builtins[] =
    "print len range str int float list dict set tuple bytes open self cls "
    "super isinstance enumerate zip map filter sorted min max sum any all type "
    "object Exception";
static const char kw_sh[] =
    "if then else elif fi case esac for while until do done in function select "
    "time return exit break continue local export readonly declare unset shift "
    "source alias echo printf read set test cd eval exec trap";
static const char kw_rust[] =
    "as async await break const continue crate dyn else enum extern false fn "
    "for if impl in let loop match mod move mut pub ref return self Self static "
    "struct super trait true type unsafe use where while";
static const char kw_rust_types[] =
    "bool char str u8 u16 u32 u64 u128 usize i8 i16 i32 i64 i128 isize f32 f64 "
    "String Vec Option Result Box Some None Ok Err";

/* The asm lexer lower-cases words before matching, so lists are lower case. */
static const char kw_asm_cpu[] =
    "aaa aad aam aas adc adcx add adox and andn bextr blsi blsmsk blsr bsf bsr "
    "bswap bt btc btr bts bzhi call cbw cdq cdqe clc cld cli clflush cmc cmova "
    "cmovae cmovb cmovbe cmovc cmove cmovg cmovge cmovl cmovle cmovna cmovnae "
    "cmovnb cmovnbe cmovnc cmovne cmovng cmovnge cmovnl cmovnle cmovno cmovnp "
    "cmovns cmovnz cmovo cmovp cmovpe cmovpo cmovs cmovz cmp cmps cmpsb cmpsd "
    "cmpsq cmpsw cmpxchg cmpxchg8b cmpxchg16b cpuid cqo crc32 cwd cwde daa das "
    "dec div enter hlt idiv imul in inc ins insb insd insw int int3 into invd "
    "invlpg iret iretd iretq ja jae jb jbe jc jcxz je jecxz jg jge jl jle jmp "
    "jna jnae jnb jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo "
    "jrcxz js jz lahf lds lea leave les lfence lfs lgdt lgs lidt lldt lmsw lock "
    "lods lodsb lodsd lodsq lodsw loop loope loopne loopnz loopz lsl lss ltr "
    "lzcnt mfence mov movbe movs movsb movsd movsq movsw movsx movsxd movzx mul "
    "mulx neg nop not or out outs outsb outsd outsw pause pdep pext pop popa popad "
    "popcnt popf popfd popfq prefetch prefetchnta prefetcht0 push pusha pushad "
    "pushf pushfd pushfq rcl rcr rdmsr rdpmc rdrand rdseed rdtsc rdtscp rep repe "
    "repne repnz repz ret retf retn rol ror rorx sahf sal sar sarx sbb scas scasb "
    "scasd scasq scasw seta setae setb setbe setc sete setg setge setl setle setna "
    "setnae setnb setnbe setnc setne setng setnge setnl setnle setno setnp setns "
    "setnz seto setp setpe setpo sets setz sfence sgdt shl shld shlx shr shrd "
    "shrx sidt sldt smsw stc std sti stos stosb stosd stosq stosw str sub swapgs "
    "syscall sysenter sysexit sysret test tzcnt ud2 verr verw wait wbinvd wrmsr "
    "xadd xchg xlat xlatb xor "
    /* ARM / AArch64 / RISC-V common mnemonics */
    "adr adrp b bl blr br bx beq bne bgt blt bge ble cbz cbnz ldr ldrb ldrh ldp "
    "str strb strh stp ldm stm movk movz mvn orr eor lsl lsr asr madd msub sdiv "
    "udiv svc eret li la addi andi ori xori slli srli srai lui auipc jal jalr "
    "lw lb lh lbu lhu sw sb sh ld sd ecall ebreak beqz bnez j jr";
static const char kw_asm_fpu[] =
    "f2xm1 fabs fadd faddp fchs fclex fcom fcomp fcompp fcos fdecstp fdiv fdivp "
    "fdivr fdivrp ffree fiadd ficom ficomp fidiv fidivr fild fimul fincstp finit "
    "fist fistp fisub fisubr fld fld1 fldcw fldenv fldl2e fldl2t fldlg2 fldln2 "
    "fldpi fldz fmul fmulp fnclex fninit fnop fnsave fnstcw fnstenv fnstsw "
    "fpatan fprem fprem1 fptan frndint frstor fsave fscale fsin fsincos fsqrt "
    "fst fstcw fstenv fstp fstsw fsub fsubp fsubr fsubrp ftst fucom fucomp "
    "fucompp fwait fxam fxch fxtract fyl2x fyl2xp1";
static const char kw_asm_regs[] =
    "al ah ax eax rax bl bh bx ebx rbx cl ch cx ecx rcx dl dh dx edx rdx "
    "si esi rsi sil di edi rdi dil bp ebp rbp bpl sp esp rsp spl ip eip rip "
    "r8 r9 r10 r11 r12 r13 r14 r15 r8d r9d r10d r11d r12d r13d r14d r15d "
    "r8w r9w r10w r11w r12w r13w r14w r15w r8b r9b r10b r11b r12b r13b r14b r15b "
    "cs ds es fs gs ss cr0 cr2 cr3 cr4 cr8 dr0 dr1 dr2 dr3 dr6 dr7 "
    "st st0 st1 st2 st3 st4 st5 st6 st7 mm0 mm1 mm2 mm3 mm4 mm5 mm6 mm7 "
    "xmm0 xmm1 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 xmm10 xmm11 xmm12 xmm13 "
    "xmm14 xmm15 ymm0 ymm1 ymm2 ymm3 ymm4 ymm5 ymm6 ymm7 ymm8 ymm9 ymm10 ymm11 "
    "ymm12 ymm13 ymm14 ymm15 "
    "x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 x10 x11 x12 x13 x14 x15 x16 x17 x18 x19 x20 "
    "x21 x22 x23 x24 x25 x26 x27 x28 x29 x30 w0 w1 w2 w3 w4 w5 w6 w7 w8 w9 "
    "r0 r1 r2 r3 r4 r5 r6 r7 lr pc xzr wzr zero ra gp tp fp "
    "a0 a1 a2 a3 a4 a5 a6 a7 s0 s1 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 "
    "t0 t1 t2 t3 t4 t5 t6";
static const char kw_asm_directives[] =
    "section segment global globl extern bits use16 use32 use64 org align alignb "
    "db dw dd dq dt do dy resb resw resd resq rest reso resy equ times incbin "
    "struc endstruc istruc iend at default rel abs cpu "
    "%define %undef %macro %endmacro %if %elif %else %endif %ifdef %ifndef "
    "%include %assign %rep %endrep %error %warning "
    ".text .data .bss .rodata .section .global .globl .extern .type .size .align "
    ".p2align .balign .byte .word .short .long .int .quad .octa .ascii .asciz "
    ".string .zero .space .skip .fill .equ .set .macro .endm .if .else .endif "
    ".ifdef .ifndef .include .file .loc .cfi_startproc .cfi_endproc .intel_syntax "
    ".att_syntax .code16 .code32 .code64 .local .comm .weak .hidden .end "
    "proc endp macro endm public ends assume model stack dup ptr offset "
    "byte word dword qword tword oword yword far near short";

static const Lang langs[] = {
    { "C",          "cpp",       ";c;h;",                    NULL, kw_c, kw_c_types, STYLES(cpp_styles), NULL, NULL },
    { "C++",        "cpp",       ";cc;cpp;cxx;hh;hpp;hxx;",  NULL, kw_c, kw_c_types, STYLES(cpp_styles), NULL, NULL },
    { "JavaScript", "cpp",       ";js;mjs;cjs;ts;jsx;tsx;",  NULL, kw_js, NULL, STYLES(cpp_styles), NULL, NULL },
    { "Java",       "cpp",       ";java;kt;cs;",             NULL, kw_java, kw_java_types, STYLES(cpp_styles), NULL, NULL },
    { "Go",         "cpp",       ";go;",                     NULL, kw_go, kw_go_types, STYLES(cpp_styles), NULL, NULL },
    { "Rust",       "rust",      ";rs;",                     NULL, kw_rust, kw_rust_types, STYLES(rust_styles), NULL, NULL },
    { "Python",     "python",    ";py;pyw;pyi;",             NULL, kw_py, kw_py_builtins, STYLES(py_styles), NULL, NULL },
    { "Shell",      "bash",      ";sh;bash;zsh;",            ";.bashrc;.profile;.zshrc;.bash_profile;", kw_sh, NULL, STYLES(sh_styles), NULL, NULL },
    { "JSON",       "json",      ";json;jsonc;",             NULL, "true false null", NULL, STYLES(json_styles), NULL, NULL },
    { "HTML",       "hypertext", ";html;htm;xhtml;",         NULL, NULL, NULL, STYLES(html_styles), NULL, NULL },
    { "XML",        "xml",       ";xml;svg;xsd;xsl;ui;glade;",NULL, NULL, NULL, STYLES(html_styles), NULL, NULL },
    { "CSS",        "css",       ";css;scss;",               NULL, NULL, NULL, STYLES(css_styles), NULL, NULL },
    { "Makefile",   "makefile",  ";mk;mak;",                 ";makefile;gnumakefile;", NULL, NULL, STYLES(make_styles), NULL, NULL },
    { "YAML",       "yaml",      ";yml;yaml;",               NULL, "true false null yes no", NULL, STYLES(yaml_styles), NULL, NULL },
    { "Markdown",   "markdown",  ";md;markdown;",            NULL, NULL, NULL, STYLES(md_styles), NULL, NULL },
    { "INI",        "props",     ";ini;conf;cfg;toml;desktop;properties;", NULL, NULL, NULL, STYLES(props_styles), NULL, NULL },
    { "Diff",       "diff",      ";diff;patch;",             NULL, NULL, NULL, STYLES(diff_styles), NULL, NULL },
    { "LaTeX",      "latex",     ";tex;latex;ltx;sty;cls;bib;dtx;ins;", NULL, NULL, NULL, STYLES(latex_styles), NULL, NULL },
    { "Assembly",   "asm",       ";asm;nasm;inc;a51;",       NULL, kw_asm_cpu, kw_asm_fpu, STYLES(asm_styles), kw_asm_regs, kw_asm_directives },
    { "Assembly (GNU)", "as",    ";s;sx;",                   NULL, kw_asm_cpu, kw_asm_fpu, STYLES(asm_styles), kw_asm_regs, kw_asm_directives },
};

/* Text intensity below 100 % mixes the colour towards the background. */
static unsigned fade(unsigned fg, unsigned bg) {
    int k = settings.text_intensity;
    if (k >= 100) return fg;
    unsigned out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        int f = (fg >> shift) & 0xff, b = (bg >> shift) & 0xff;
        out |= (unsigned)(b + (f - b) * k / 100) << shift;
    }
    return out;
}

/* Above 100 % the font weight grows: 170 % turns regular 400 into ~680. */
static int weight(int flags) {
    int extra = settings.text_intensity > 100 ? (settings.text_intensity - 100) * 4 : 0;
    int w = ((flags & B) ? SC_WEIGHT_BOLD : SC_WEIGHT_NORMAL) + extra;
    return w > 900 ? 900 : w;
}

static void style(Doc *d, int s, unsigned fg, unsigned bg, int flags) {
    sci_send(d, SCI_STYLESETFORE, s, RGB(fade(fg, bg)));
    sci_send(d, SCI_STYLESETBACK, s, RGB(bg));
    sci_send(d, SCI_STYLESETWEIGHT, s, weight(flags));
    sci_send(d, SCI_STYLESETITALIC, s, (flags & I) != 0);
}

#define OPAQUE(c)   (RGB(c) | 0xff000000u)

void lang_apply_base(Doc *d) {
    const Theme *t = settings_theme();

    /* Code page, tabs and indentation belong to the Scintilla document, so
       they are set here too, where a swapped-in document gets restyled. */
    sci_send(d, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    sci_send(d, SCI_SETTABWIDTH, settings.tab_width, 0);
    sci_send(d, SCI_SETUSETABS, !settings.use_spaces, 0);

    sci_send(d, SCI_STYLERESETDEFAULT, 0, 0);
    sci_send(d, SCI_STYLESETFONT, STYLE_DEFAULT, (sptr_t)settings.font);
    sci_send(d, SCI_STYLESETSIZEFRACTIONAL, STYLE_DEFAULT,
             (sptr_t)(settings.font_size * SC_FONT_SIZE_MULTIPLIER + 0.5));
    style(d, STYLE_DEFAULT, t->syn[R_FG], t->bg, 0);
    sci_send(d, SCI_STYLECLEARALL, 0, 0);
    style(d, STYLE_LINENUMBER, t->line_no, t->margin, 0);
    style(d, STYLE_BRACELIGHT, t->syn[R_KEYWORD], t->caret_line, B);

    if (settings.highlight_line)
        sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, OPAQUE(t->caret_line));
    else
        sci_send(d, SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0);
    sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, OPAQUE(t->sel));
    sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, OPAQUE(t->sel));
    sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, RGB(t->sel) | 0x80000000u);
    sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, OPAQUE(t->syn[R_FG]));
    sci_send(d, SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE, OPAQUE(t->whitespace));
    sci_send(d, SCI_SETVIEWWS, settings.show_whitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE, 0);
    sci_send(d, SCI_SETWHITESPACESIZE, 2, 0);
    sci_send(d, SCI_SETVIEWEOL, settings.show_whitespace, 0);

    /* Search matches: filled rounded box under the text. */
    sci_send(d, SCI_INDICSETSTYLE, IND_FIND, INDIC_ROUNDBOX);
    sci_send(d, SCI_INDICSETFORE, IND_FIND, RGB(t->find));
    sci_send(d, SCI_INDICSETALPHA, IND_FIND, 110);
    sci_send(d, SCI_INDICSETOUTLINEALPHA, IND_FIND, 220);
    sci_send(d, SCI_INDICSETUNDER, IND_FIND, 1);
}

static gboolean in_list(const char *list, const char *item) {
    if (!list || !*item) return FALSE;
    size_t n = strlen(item);
    for (const char *p = strstr(list, item); p; p = strstr(p + 1, item))
        if (p[-1] == ';' && p[n] == ';') return TRUE;
    return FALSE;
}

static const Lang *find_lang(const char *path) {
    if (!path) return NULL;
    char *base = g_ascii_strdown(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, -1);
    const char *dot = strrchr(base, '.');
    const Lang *found = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(langs) && !found; i++) {
        if (in_list(langs[i].files, base) ||
            (dot && dot != base && in_list(langs[i].exts, dot + 1)))
            found = &langs[i];
    }
    g_free(base);
    return found;
}

void lang_apply_for_path(Doc *d, const char *path) {
    const Lang *l = find_lang(path);

    /* Restyle from scratch so a Save As to another type leaves no stale colours. */
    lang_apply_base(d);

    if (!l) {
        sci_send(d, SCI_SETILEXER, 0, 0);
        d->lang = "Plain Text";
        return;
    }
    sci_send(d, SCI_SETILEXER, 0, (sptr_t)CreateLexer(l->lexer));
    if (l->kw0) sci_send(d, SCI_SETKEYWORDS, 0, (sptr_t)l->kw0);
    if (l->kw1) sci_send(d, SCI_SETKEYWORDS, 1, (sptr_t)l->kw1);
    if (l->kw2) sci_send(d, SCI_SETKEYWORDS, 2, (sptr_t)l->kw2);
    if (l->kw3) sci_send(d, SCI_SETKEYWORDS, 3, (sptr_t)l->kw3);
    for (size_t i = 0; i < l->nstyles; i++)
        style(d, l->styles[i].style, settings_theme()->syn[l->styles[i].role],
              settings_theme()->bg, l->styles[i].flags);
    d->lang = l->name;
}
