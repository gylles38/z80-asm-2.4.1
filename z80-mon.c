/* MAIN PART OF MONITOR */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "z80-cpu.h"
#include "execute.h"
#include "console_token"
#include "console.h"
#include "regs.h"
#include "file.h"
#include "asm.h"
#include "decode.h"
#include "memory.h"
#include "ports.h"
#include "interrupt.h"
#include "mini-display.h"
#include "keyboard.h"
#include "hash.h"
#include "expression.h"
#include "hardware/includes"
#include <sys/stat.h>

static _ushort MEMP, MEMD, MEMBKP;

static char* msg1 = "Z80 monitor V 2.4.1\n(c)1999-2004 Brainsoft\n";
static char* msg2 = "Z80 monitor (GPL)  " TIME_STAMP;
static char _string[257];
static char* string = _string + 1;
static char load_name[256];
static char save_name[256];
static char proto_name[256];
static FILE* stream;

static unsigned speed;
static int keyboard_disabled;
static int current_pc;
static int follow;
static int row;     /* row for printing next instruction */
static _uchar tmp_memory[64];
static unsigned long  last_ticks;
#define  MAX_BRK_PTS  16
static unsigned int max_brk_pts = 0 ;
static unsigned long  breakpt[MAX_BRK_PTS]; // maximum 16 points d'arrêts
#define row_warning_display 45 // ligne ou s'affiche les messages 
struct info { char* label; int value; unsigned lineno; };
static struct info* ele;
static unsigned  labels, next_label_index;

struct TAG
{
   char tagName[25];
   int  tagValue;
   char tagHex[8];
};

struct TAG tab_tags[100];

int tag_line = 0; // pour insertion de chaque tag dans le tableau

char decimal[10];

enum
{
   DEFAULT, TOKEN, ADDR, HOT, GARISH, WARN, MEMORY, STATUS, HIDDEN, TAG
};
static unsigned char color[] =
{ WHITE,  GREEN,YELLOW,RED,BRIGHT,YELLOW,BLUE,  PURPLE,GRAY, CYAN };

#define KEYBOARD_MAP_FILE "keyboard_map"


static void warning(char* message)
{
   static char txt[512];
   /* c_bell(); */
   c_goto(0, row_warning_display);
   c_setcolor(color[WARN]);
   sprintf(txt, "%s", message);
   c_print(txt);
   usleep(2000000L);
   c_setcolor(color[DEFAULT]);
   c_clear(0, row_warning_display, 79, row_warning_display);
}


static void error_msg(char* line, char* message)
{
   static char txt[512];
   c_bell();
   c_goto(0, 78);
   c_setcolor(color[HOT]);
   sprintf(txt, "Error: %s %s", message, line);
   c_print(txt);
   usleep(1000000L);
   c_setcolor(color[DEFAULT]);
   c_clear(0, row_warning_display, 79, row_warning_display);
}

/*** called e.g. from decode(...) in simul.c or compile() defined in asm.c ***/
void error(int n, const char* line, char* message)
{
   static char txt[512];
   unsigned char k;
   n = n;
   c_bell();
   c_goto(40, row_warning_display);
   c_print(line);
   c_goto(0, row_warning_display);
   c_setcolor(color[HOT]);
   sprintf(txt, "Error: %s", message);
   c_print(txt);
   c_setcolor(color[DEFAULT]);
   c_print(" hit any key");
   k = c_getkey();
   c_clear(0, row_warning_display, 79, row_warning_display);
}


static char
__printable(char c)
{
   return (c >= 32 && c < 127) ? c : '.';
}


/* replace string in another string */
void replaceWord(char* str, char* oldWord, char* newWord)
{
    char *pos, temp[1000];
    int index = 0;
    int owlen;
 
    owlen = strlen(oldWord);
 
    // Repeat This loop until all occurrences are replaced.
 
    while ((pos = strstr(str, oldWord)) != NULL) {
        // Bakup current line
        strcpy(temp, str);
 
        // Index of current found word
        index = pos - str;
 
        // Terminate str after word found index
        str[index] = '\0';
 
        // Concatenate str with new word
        strcat(str, newWord);
 
        // Concatenate str with remaining words after
        // oldword found index.
        strcat(str, temp + index + owlen);
    }
}

/* prints instruction */
void
print(char* str, _ushort begin_pc, _ushort arg1, _ushort arg2)
{
   int boucle;

   if (row >= 0)
   {
      char txt[256];

      bool bkp = 0;
      char tag_arg1[25] = "";
      char tag_arg2[25] = "";

// begin_pc contient l'adresse de début de l'instruction affichée, current_pc l'adresse après exécution de l'instruction
      if (max_brk_pts > 0)
      {
         // Affichage des points d'arrêts avec une * avant l'adresse
         boucle = 0;
         while (boucle < max_brk_pts && begin_pc != (breakpt[boucle] & MAX_ADDR))
         {
            boucle++;
         }
         if (boucle < max_brk_pts)
         {
         // Il existe un point d'arrêt à l'adresse begin_pc
            bkp = 1;
         }
      }

      if (tag_line > 0) // Pas de fichier symbol chargé
      {
         // Décodage des étiquettes dans les instructions
         boucle = 0;
         while (boucle < tag_line && arg1 != tab_tags[boucle].tagValue)
         {
            boucle++;
         }
         if (boucle < tag_line)
         {
            // Remplace une chaine affichée en Hexa
            replaceWord(str, tab_tags[boucle].tagHex, tab_tags[boucle].tagName);
            // Remplace une chaine affichée en Décimal
            sprintf(decimal, "%d", tab_tags[boucle].tagValue);
            replaceWord(str, decimal, tab_tags[boucle].tagName);
         }

         boucle = 0;
         while (boucle < tag_line && arg2 != tab_tags[boucle].tagValue)
         {
            boucle++;
         }
         if (boucle < tag_line)
         {
            // Remplace une chaine affichée en Hexa
            replaceWord(str, tab_tags[boucle].tagHex, tab_tags[boucle].tagName);
            // Remplace une chaine affichée en Décimal
            sprintf(decimal, "%d", tab_tags[boucle].tagValue);
            replaceWord(str, decimal, tab_tags[boucle].tagName);
         }

         // Décodage des étiquettes 
         boucle = 0;
         while (boucle < tag_line && current_pc != tab_tags[boucle].tagValue)
         {
            boucle++;
         }
         if (boucle < tag_line)
         {
            // Il existe une étiquette à l'adresse current_pc
            if (bkp == 1)
            // Il existe également un break point
            {
               sprintf(txt, "*%04x  %08s ", current_pc, tab_tags[boucle].tagName);
            }

            else 
            {
               sprintf(txt, " %04x  %08s ", current_pc, tab_tags[boucle].tagName);
            }
         }
         else{
            if (bkp == 1)
            // Il existe également un break point
            {
               sprintf(txt, "*%04x  %08s ", current_pc, "        ");
            }
            else
            {
               sprintf(txt, " %04x  %08s ", current_pc, "        ");
            }
         }
      }
      else {
         if (bkp == 1)
         {
            sprintf(txt, "*%04x ", current_pc);
         }
         else
         {
            sprintf(txt, " %04x ", current_pc);
         }
      }

      c_goto(74, row - 1);
      c_setcolor(row == 22 ? color[GARISH] : color[ADDR]);
      c_print(txt);
      c_setcolor(row == 22 ? color[GARISH] : color[DEFAULT]);
      c_print(str);
   }
   else if (row == -1)
      fprintf(stream, "%04x %-14s", current_pc, str);
   else if (row == -2 && (MODE & 16))
      ; /* first pass disassembling */
   else if (row == -2)
   {
      int i;
      fprintf(stream, "%04x  ", current_pc);
      for (i = 0; i < (PC ? PC : MAX_MEM) - current_pc && i < 4; i++)
         fprintf(stream, "%02x ", (unsigned)memory_at((_ushort)(current_pc + i)));
      for (;i < 4;i++)
         fprintf(stream, "   ");
      for (i = 0; i < (PC ? PC : MAX_MEM) - current_pc && i < 4; i++)
      {
         unsigned char  m = memory_at((_ushort)(current_pc + i));
         fprintf(stream, "%c", (m >= 32 && m < 127 ? m : ' '));
      }
      for (;i < 4;i++)
         fprintf(stream, " ");
      if (MODE & 8)  /* second pass disassembling */
      {
         while (next_label_index < labels && ele[next_label_index].value < current_pc)
            next_label_index++;
         if (next_label_index >= labels || ele[next_label_index].value > current_pc)
            fprintf(stream, "        ");
         else
         {
            fprintf(stream, " %5s: ", ele[next_label_index].label);
            ele[next_label_index].label[0] = 0;
            next_label_index += 1;
            while (next_label_index < labels &&
               ele[next_label_index].value == ele[next_label_index - 1].value)
               ele[next_label_index].label[0] = 0;
         }
      }
      fprintf(stream, " %-s", str);
   }
}


static void
__print_r16(int x, int y, char* t, int R)
{
   char txt[256];
   c_goto(x, y);
   sprintf(txt, "%s=", t);
   c_setcolor(color[TOKEN]);
   c_print(txt);
   sprintf(txt, "0x%04x %05u", R, R);
   c_setcolor(color[DEFAULT]);
   c_print(txt);
}


static void
__print_rr(int x, int y, char* t, int R)
{
   char txt[256];
   c_goto(x, y);
   sprintf(txt, "%s=", t);
   c_setcolor(color[TOKEN]);
   c_print(txt);
   sprintf(txt, "x%02xx%02x %05u", R >> 8 & 255, R & 255, R);
   c_setcolor(color[DEFAULT]);
   c_print(txt);
}


static void
tobin(char* s, _uchar x)
{
   int a;
   for (a = 0;a < 8;a++)
      s[7 - a] = ((x & (1 << a)) >> a) ? '1' : '0';
   s[8] = 0;
}


static void
__print_x(int x, int y, char* t, _uchar R)
{
   char txt[256];
   char b[256];
   tobin(b, R);
   c_goto(x, y);
   sprintf(txt, "%s=", t);
   c_setcolor(color[TOKEN]);
   c_print(txt);
   sprintf(txt, "x%02x (%s)", (unsigned)R, b);
   c_setcolor(color[DEFAULT]);
   c_print(txt);
}


static void
__print_r(int x, int y, char* t, _uchar R)
{
   char txt[256];
   char b[256];
   tobin(b, R);
   c_goto(x, y);
   c_setcolor(color[TOKEN]);
   sprintf(txt, "%s=", t);
   c_print(txt);
   c_setcolor(color[DEFAULT]);
   sprintf(txt, "%03d", R);
   c_print(txt);
   c_setcolor(color[MEMORY]);
   sprintf(txt, "%c", __printable(R));
   c_print(txt);
   c_setcolor(color[DEFAULT]);
   sprintf(txt, "(%s)", b);
   c_print(txt);
}


static void
print_generell_regs(void)
{
   __print_rr(30, 2, "AF", (A << 8) + F);
   __print_rr(30, 3, "BC", (B << 8) + C);
   __print_rr(30, 4, "DE", (D << 8) + E);
   __print_rr(30, 5, "HL", (H << 8) + L);

   __print_rr(47, 2, "AF'", (A_ << 8) + F_);
   __print_rr(47, 3, "BC'", (B_ << 8) + C_);
   __print_rr(47, 4, "DE'", (D_ << 8) + E_);
   __print_rr(47, 5, "HL'", (H_ << 8) + L_);

   __print_r(30, 12, "A", A);
   __print_r(30, 13, "F", F);
   __print_r(30, 14, "B", B);
   __print_r(30, 15, "C", C);
   __print_r(30, 16, "D", D);
   __print_r(30, 17, "E", E);
   __print_r(30, 18, "H", H);
   __print_r(30, 19, "L", L);

   __print_r(47, 12, "A'", A_);
   __print_r(47, 13, "F'", F_);
   __print_r(47, 14, "B'", B_);
   __print_r(47, 15, "C'", C_);
   __print_r(47, 16, "D'", D_);
   __print_r(47, 17, "E'", E_);
   __print_r(47, 18, "H'", H_);
   __print_r(47, 19, "L'", L_);

}


static void
print_index_regs(void)
{
   __print_r16(40, 7, "IX", IX);
   __print_r16(40, 8, "IY", IY);
}


static void
print_sp_and_pc(void)
{
   __print_r16(40, 9, "SP", SP);
   __print_r16(40, 10, "PC", PC);
}


static void
print_special_regs(void)
{
   __print_x(31, 21, "I", I);
   __print_x(48, 21, "R", R);
}


static void
print_regs(void)
{
   print_generell_regs();
   print_index_regs();
   print_sp_and_pc();
   print_special_regs();
}


static void
print_flags(void)
{
   char txt[256];

   c_goto(29, 0);
   c_setcolor(is_flag(F_C) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_C) ? " C " : "NC ");
   c_setcolor(is_flag(F_Z) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_Z) ? " Z " : "NZ ");
   c_setcolor(is_flag(F_M) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_P) ? " P " : " M ");
   c_setcolor(is_flag(F_PE) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_PE) ? "PE " : "PO ");
   c_setcolor(is_flag(F_H) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_H) ? " H " : "NH ");
   c_setcolor(is_flag(F_N) ? color[GARISH] : color[DEFAULT]);
   c_print(is_flag(F_N) ? " N " : "NN ");

   c_setcolor(color[TOKEN]);
   c_goto(57, 0);
   c_print("IM=");
   sprintf(txt, "%d", IM);
   c_setcolor(color[DEFAULT]);
   c_goto(60, 0);
   c_print(txt);

   c_setcolor(color[TOKEN]);
   c_goto(49, 0);
   c_print("IFF=");
   c_goto(53, 0);
   c_setcolor(IFF1 ? color[GARISH] : color[DEFAULT]);
   c_print(IFF1 ? "EI" : "DI");
   c_setcolor(color[DEFAULT]);

}


static void
print_instr(void)
{
   _ushort old_pc = PC;
   _ushort begin_pc = PC; // permet de connaitre l'adresse de début de l'instruction dans la boucle ci-dessous

   c_clear(74, 20, 115, 42);
   for (row = 22;row < 44;row++)
   {
      current_pc = PC;
      decode(0, 0, begin_pc); // Affiche l'instruction
      begin_pc = PC;
   }
   c_goto(80, 20); // 7,0 avant mes modifs
   c_setcolor(color[TOKEN]);
   c_print("MNEMONIC");

   c_setcolor(color[HIDDEN]);
   if ((MODE & 3) == 1) c_print("d");
   else if ((MODE & 3) == 2)  c_print("h");
   else if ((MODE & 3) == 3)  c_print("x");
   else c_print(" ");
   if ((MODE & 12) == 4) c_print("a");
   else if ((MODE & 12) == 8)  c_print("l");
   else if ((MODE & 12) == 12)  c_print("L");
   else c_print("r");
   c_goto(73, 21);
   c_setcolor(color[GARISH]);
   c_print(">");
   c_setcolor(color[DEFAULT]);
   PC = old_pc;
}


static void
print_stack(void)
{
   char txt[256];
   _ushort b, c, d;
   int r, a;

   c_goto(36, 24);
   c_setcolor(color[TOKEN]);
   c_print("STACK");
   c_setcolor(color[DEFAULT]);
   for (r = 25, a = SP + 25;a >= SP - 6;a -= 2, r++)
   {
      c = a & MAX_ADDR;
      d = (a + 1) & MAX_ADDR;
      b = memory_at(c) | (memory_at(d) << 8);
      sprintf(txt, "%04x  ", c);
      c_goto(30, r);
      c_setcolor(r == 31 ? color[GARISH] : color[ADDR]);
      c_print(txt);
      c_setcolor(r == 31 ? color[GARISH] : color[DEFAULT]);
      sprintf(txt, "%05d %04x ", b, b);
      c_print(txt);
      sprintf(txt, "%c%c", __printable(memory_at(d)), __printable(memory_at(c)));
      c_setcolor(color[MEMORY]);
      c_print(txt);
      c_setcolor(color[DEFAULT]);
   }
   c_goto(29, 31);
   c_setcolor(color[GARISH]);
   c_print(">");
   c_setcolor(color[DEFAULT]);
}

static void
print_mem(void)
{
   char txt[256];
   _ushort a;
   int r, c, boucle;
   _ushort memp;

   _ushort memcols, cols; // nombre de case mémoires a afficher par colonne, nombres de colonnes

   memcols = 1;
   cols = 2;

   if (follow)memp = PC;
   else memp = MEMP;

   c_goto(0, 0);
   c_setcolor(color[TOKEN]);
   c_print("<------ MEMORY ------>");
   for (r = 0, c = memp;c < memp + 42;c += memcols, r++) // 120 = nombre d'adresses mémoires affichées
   {
      _ushort b;
      a = c & MAX_ADDR;
      sprintf(txt, "%04x %05d", a, c);
      // c contient l'adresse en décimal à comparer avec le tableau des tags
      c_goto(0, 1 + r);
      c_setcolor(color[ADDR]);
      c_print(txt);

      c_setcolor(color[TAG]);

      boucle = 0;
      while (boucle < tag_line && c != tab_tags[boucle].tagValue)
      {
         boucle++;
      }
      if (boucle < tag_line)
      {
         sprintf(txt, " %08s ", tab_tags[boucle].tagName);
      }
      else{
         sprintf(txt, "          ");
      }
      c_print(txt);

      c_setcolor(color[DEFAULT]);
      for (b = 0;b < memcols;b++)
         sprintf(txt + 3 * b, "%02x ", (unsigned)memory_at(a + b));
      c_print(txt);
      for (b = 0;b < memcols;b++)
         sprintf(txt + b, "%c", __printable(memory_at(a + b)));
      c_setcolor(color[MEMORY]);
      c_print(txt);
      c_setcolor(color[DEFAULT]);

   }
}

static void
print_screen(void)
{
   char txt[256];
   _ushort a;
   int r, c;
   _ushort memd;

   _ushort memcols, physcols;

   memcols = 20;
   physcols = 134;

   if (follow)memd = PC;
   else memd = MEMD;

   c_goto(physcols, 0);
   c_setcolor(color[TOKEN]);
   //   c_print("1........2.........3.........4.........5.........6.........7.........8");

   // COLVIRT EQU 20 ; Ligne suivante de l'écran (le nombre de colonnes réelles est < nombre de colonnes maxi (ex : 40 colonnes à l'écran mais 64 pour passer à la ligne suivante)
   // COLREAL EQU 15 ; 

      // Affiche la simulation d'un écran de 20 colonnes de 19 lignes
   c_print("123456789ABCDEF....."); // 20 colonnes dont 15 physiques (celles sans les .)
   for (r = 0; r < 19; r++)
   {
      c_goto(physcols + 20, r);
      if (r == 0)
      {
         sprintf(txt, "|");
      }
      else
      {
         sprintf(txt, "|%u", r);
      }
      c_print(txt);
   }

   // Affiche les valeurs hexa et ascii
   for (r = 0, c = memd;c < memd + 360;c += memcols, r++)
   {
      _ushort b;
      a = c & MAX_ADDR;
      sprintf(txt, "%04x %05d ", a, c); // colonne adresses
      c_goto(74, 1 + r);
      c_setcolor(color[ADDR]);
      c_print(txt);
      c_setcolor(color[DEFAULT]);
      for (b = 0;b < memcols;b++)
         sprintf(txt + 3 * b, "%02x ", (unsigned)memory_at(a + b));  // memcols colonnes pour le contenu de la ligne

      c_print(txt);

      for (b = 0;b < memcols;b++)
         sprintf(txt + b, "%c", __printable(memory_at(a + b))); // memcols colonnes pour le contenu de la ligne         

      c_setcolor(color[MEMORY]);
      c_goto(physcols, 1 + r);
      c_print(txt);
      c_setcolor(color[DEFAULT]);
   }
}


static void
print_speed(void)
{
   c_setcolor(color[STATUS]);
   c_goto(11, row_warning_display);
   if (speed)
   {
      char txt[12];
      double  freq = speed * 0.0025;
      if (freq < 1.0)
         sprintf(txt, " %4.0f kHz ", speed * 2.5);
      else if (freq < 10.0)
         sprintf(txt, "%5.3f MHz ", freq);
      else
         sprintf(txt, "%5.2f MHz ", freq);
      c_print(txt);
   }
   else
      c_print("      ");
   c_setcolor(color[DEFAULT]);
}


static void
print_status(void)
{
   c_goto(0, row_warning_display);
   c_setcolor(color[STATUS]);
   c_print(follow ? "FOLLOW " : "       ");
   c_goto(7, row_warning_display);
   c_print(!cpu_is_in_disassemble ? "RUN " : "    ");
   print_speed();
   c_setcolor(color[STATUS]);
   c_goto(20, row_warning_display);
   c_print(stream ? "PROTO " : "      ");
   c_setcolor(color[HOT]);
   c_goto(26, row_warning_display);
   c_print(keyboard_disabled ? "NOKEYS" : "      ");
   c_setcolor(color[DEFAULT]);
}


void
print_ticks(void)
{
   char txt[16];
   sprintf(txt, "%10lu", ticks);
   c_setcolor(color[DEFAULT]);
   c_goto(25, 10);
   c_print(txt);
   c_setcolor(cpu_pin[busack] ? color[HOT] : color[TOKEN]);
   c_print(" T");
   last_ticks = ticks;
   sprintf(txt, "%10lu", cycles);
   c_setcolor(color[DEFAULT]);
   c_goto(25, 8);
   c_print(txt);
   c_setcolor(color[TOKEN]);
   c_print(" M");
}


static void
print_breaks(void)
{
   int i, j;
   char txt[16];

   c_goto(55, 24);
   c_setcolor(color[TOKEN]);
   c_print("BREAKPOINTS");
   c_setcolor(color[DEFAULT]);

   for (j = i = 0;i < MAX_BRK_PTS;i++)
      if (breakpt[i] >> 16)
      {
         sprintf(txt, " #%2d  ", i+1);
         c_setcolor(color[HOT]);
         c_goto(52, 25 + (1*j));
         c_print(txt);
         sprintf(txt, "%04x", (unsigned)breakpt[i] & MAX_ADDR);
         c_setcolor(color[ADDR]);
         c_print(txt);
         j++;
         c_setcolor(color[DEFAULT]);
      }
      else{
         sprintf(txt, " #%2d:%6s", i+1, " ");
         c_setcolor(color[GREEN]);
         c_goto(41, 25 + (1*j));
         c_print(txt);
         c_setcolor(color[DEFAULT]);
         c_print(txt);
         j++;
         c_setcolor(color[DEFAULT]);
      }
}


static void print_halt(void)
{
   c_setcolor(color[HOT]);
   c_goto(78, 0);
   c_print(cpu_pin[halt] ? "H" : " ");
   c_setcolor(color[DEFAULT]);
}


/* draw screen */
static void
print_panel(void)
{
   print_regs();

   print_flags();
   print_instr();

   print_stack();
   print_mem(); // affiche la mémoire réservée aux variables du programme
   print_status();
   print_ticks();

   print_breaks();

   print_halt();

   print_screen(); // affiche la mémoire écran
}


static void
show_keys(void)
{
   int i = 0;
   c_cls();
#include  "help_layout"
   c_goto(0, row_warning_display);
   c_print("PRESS ANY KEY TO QUIT HELP.");
   c_goto(79 - strlen(msg2), row_warning_display);
   c_print(msg2);
   c_getkey();
   c_cls();
}


/* clear all general purpos registers and IX, IY, and SP */
static void
clear_user_regs(void)
{
   A = 0;B = 0;C = 0;D = 0;E = 0;F = 0;H = 0;L = 0;
   A_ = 0;B_ = 0;C_ = 0;D_ = 0;E_ = 0;F_ = 0;H_ = 0;L_ = 0;
   SP = 0;IX = 0;IY = 0;
}


/* put SP to 0xFFFC and store 4 bytes starting at 0xFFFC */
static void
stack_halt(void)
{
   _ushort d;
   SP = 0xfffc;
   d = SP + 2;
   write_memo(SP, d & 255);
   write_memo(SP + 1, d >> 8);
   write_memo(SP + 2, 0xf3); /* DI */
   write_memo(SP + 3, 0x76); /* HALT */
}


static void
ask_flag(void)
{
   unsigned char c;
   c_clear(0, row_warning_display, 79, row_warning_display);
   c_goto(0, row_warning_display);
   c_print("Toggle flag: Zero, Carry, Parity, Sign, H, N?");
   c = c_getkey();
   switch (c)
   {
   case 'z':
   case 'Z':
      set_flag(is_flag(F_Z) ? F_NZ : F_Z);
      break;

   case 'c':
   case 'C':
      set_flag(is_flag(F_C) ? F_NC : F_C);
      break;

   case 'p':
   case 'P':
      set_flag(is_flag(F_PE) ? F_PO : F_PE);
      break;

   case 's':
   case 'S':
      set_flag(is_flag(F_M) ? F_P : F_M);
      break;

   case 'h':
   case 'H':
      set_flag(is_flag(F_H) ? F_NH : F_H);
      break;

   case 'n':
   case 'N':
      set_flag(is_flag(F_N) ? F_NN : F_N);
      break;

   default:
      c_bell();
      break;
   }
   c_clear(0, row_warning_display, 79, row_warning_display);
}


/* reads string from input (will be stored in pointer), writes prompt message */
/* string cannot be longer than max_len */
/* return value: 0=ok, 1=escape pressed */
static int
ask_str(char* pointer, char* message, int max_len)
{
   unsigned char c;
   int a = strlen(pointer), l = strlen(message);

   c_clear(0, row_warning_display, 79, row_warning_display);
   c_cursor(C_NORMAL);
   c_goto(0, row_warning_display);
   c_setcolor(color[GARISH]);
   c_print(message);
   c_setcolor(color[DEFAULT]);
   c_print(pointer);
   while (1)
   {
      c = c_getkey();
      if (c == K_ESCAPE)
      {
         pointer[0] = 0;
         c_clear(0, row_warning_display, 79, row_warning_display);
         c_cursor(C_HIDE);
         return 1;
      }
      if (c == K_ENTER)
      {
         c_clear(0, row_warning_display, 79, row_warning_display);
         c_cursor(C_HIDE);
         return 0;
      }
      if (c == K_BACKSPACE && a)
      {
         a--;
         pointer[a] = 0;
         c_clear(l + strlen(pointer), row_warning_display, 79, row_warning_display);
         c_goto(l + strlen(pointer), row_warning_display);
         continue;
      }
      if (c >= 32 && a < max_len)
      {
         pointer[a] = c;   /* do charu se dava unsigned char */
         a++;
         pointer[a] = 0;
         c_goto(l, row_warning_display);
         c_print(pointer);
         c_clear(l + strlen(pointer), row_warning_display, 79, row_warning_display);
         c_goto(l + strlen(pointer), row_warning_display);
      }
   }
}


/* tries to convert a string to an unsigned number */
/* on error returns ~0 */
static unsigned  convert_to_uns(char* txt)
{
   unsigned  i, j, val;
   for (i = 0;txt[i] == ' ' || txt[i] == '\t';i++);
   if (!txt[i] || txt[i] == '+' || txt[i] == '-')  return ~0;
   j = test_number(txt + i, &val); /* Single character-representation '?' is allowed */
   if (!j)  return ~0;
   return val;
}


/* reads a non negative integer from input */
/* on escape or error returns ~0 */
static unsigned
ask(char* str, unsigned init_val)
{
   static char txt[256];

   if (init_val)sprintf(txt, "%u", init_val);
   else txt[0] = 0;
   if (ask_str(txt, str, 16))return ~0;
   return  convert_to_uns(txt);
}


/* reads a non negative integer from input */
/* on escape or error returns ~0 */
static unsigned
ask_x(char* str, unsigned init_val)
{
   static char txt[256];

   if (init_val)sprintf(txt, "0x%x", init_val);
   else txt[0] = 0;
   if (ask_str(txt, str, 16))return ~0;
   return  convert_to_uns(txt);
}


static void
ask_general_16_register(char* prompt, _uchar* high, _uchar* low)
{
   unsigned x;
   x = ask(prompt, *high << 8 | *low);
   if (x > MAX_ADDR)
   {
      c_bell();print_status();
   }
   else
   {
      *high = x >> 8;
      *low = x & 255;
      cpu_is_in_disassemble = 1;
   }
}


static void
ask_special_16_register(char* prompt, _ushort* reg16)
{
   unsigned x;
   x = ask(prompt, *reg16);
   if (x > MAX_ADDR)
   {
      c_bell();print_status();
   }
   else
   {
      *reg16 = x;
      cpu_is_in_disassemble = 1;
   }
}


static void
ask_16bit_register(void)
{
   unsigned char c;
   c_clear(0, row_warning_display, 79, row_warning_display);
   do {
      c_goto(0, row_warning_display);
      c_print("Toggle Register: bc, de, hl, BC, DE, HL, xX, yY, Sp");
      c = c_getkey();
      switch (c)
      {
      case 'b':  ask_general_16_register("BC=", &B, &C);
         break;

      case 'B':  ask_general_16_register("BC'=", &B_, &C_);
         break;

      case 'd':  ask_general_16_register("DE=", &D, &E);
         break;

      case 'D':  ask_general_16_register("DE'=", &D_, &E_);
         break;

      case 'h':  ask_general_16_register("HL=", &H, &L);
         break;

      case 'H':  ask_general_16_register("HL'=", &H_, &L_);
         break;

      case 'x': case 'X':
         ask_special_16_register("IX=", &IX);
         break;

      case 'y': case 'Y':
         ask_special_16_register("IY=", &IY);
         break;

      case 's': case 'S':
         ask_special_16_register("SP=", &SP);
         break;

      case K_ENTER:
         break;

      default:   c_bell();
         break;
      }
      print_regs();
   } while (c != K_ENTER);
   c_clear(0, row_warning_display, 79, row_warning_display);
}


static void
ask_8bit_register(char* prompt, _uchar* reg8)
{
   unsigned x = ask(prompt, *reg8);
   if (x > 255)
   {
      c_bell();print_status();
   }
   else
   {
      *reg8 = x;
      cpu_is_in_disassemble = 1;
   }
}


static void
ask_register(void)
{
   unsigned char c;
   c_clear(0, row_warning_display, 79, row_warning_display);
 //  do {
      c_goto(0, row_warning_display);
      c_print("Toggle Register: a, f, b, c, d, e, h, l, "
         "A, F, B, C, D, E, H, L, =, I, R");
      c = c_getkey();
      switch (c)
      {
      case '=':  ask_16bit_register();
         break;

      memset(A, 0, sizeof(A));
      memset(A_, 0, sizeof(A_));
      case 'a':  ask_8bit_register("A=", &A);
         break;

      case 'A':  ask_8bit_register("A'=", &A_);
         break;

      case 'f':  ask_8bit_register("F=", &F);
         print_flags();
         break;

      case 'F':  ask_8bit_register("F'=", &F_);
         print_flags();
         break;

      case 'b':  ask_8bit_register("B=", &B);
         break;

      case 'B':  ask_8bit_register("B'=", &B_);
         break;

      case 'c':  ask_8bit_register("C=", &C);
         break;

      case 'C':  ask_8bit_register("C'=", &C_);
         break;

      case 'd':  ask_8bit_register("D=", &D);
         break;

      case 'D':  ask_8bit_register("D'=", &D_);
         break;

      case 'e':  ask_8bit_register("E=", &E);
         break;

      case 'E':  ask_8bit_register("E'=", &E_);
         break;

      case 'h':  ask_8bit_register("H=", &H);
         break;

      case 'H':  ask_8bit_register("H'=", &H_);
         break;

      case 'l':  ask_8bit_register("L=", &L);
         break;

      case 'L':  ask_8bit_register("L'=", &L_);
         break;

      case 'I':  ask_8bit_register("I=", &I);
         break;

      case 'R':  ask_8bit_register("R=", &R);
         break;

      case K_ENTER:
         break;

      default:   c_bell();
         break;
      }
      print_regs();
 //  } while (c != K_ENTER);
   c_clear(0, row_warning_display, 79, row_warning_display);
}


static void
protocol(void)
{
   row = -1;
   current_pc = PC;
   decode(0, 0, 0);
   fprintf(stream, "%c%c%c%c ", (is_flag(F_C) ? 'C' : '.'), (is_flag(F_Z) ? 'Z' : '.')
      , (is_flag(F_M) ? '-' : '+'), (is_flag(F_PE) ? 'e' : 'o'));
   fprintf(stream, "%02x %02x %02x %02x %02x %02x %02x ", A, B, C, D, E, H, L);
   fprintf(stream, "%02x %02x %02x %02x %02x %02x %02x ", A_, B_, C_, D_, E_, H_, L_);
   fprintf(stream, "%04x %04x %04x\n", IX, IY, SP);
   fflush(stream);
   PC = current_pc;
}


void finish(int signo)
{
   c_shutdown();
   exit(-signo);
}


static int compare_addr(const struct info* left, const struct info* right)
{
   return  left->value == right->value ? 0 : left->value < right->value ? -1 : 1;
}

static void
load_symbols(void)
{
   struct stat sb;
   stat(load_name, &sb);

   char* file_contents = malloc(sb.st_size);
   int compteur = 1;
   bool begin = 0;
   char txt[256];

   while (fscanf(stream, "%[^\n ] ", file_contents) != EOF && begin == 0)
   {
      if (strcmp(file_contents, "line") == 0) {
         begin = 1;
      }
   }

   strcpy(tab_tags[tag_line].tagName, file_contents); // premier tag

   while (fscanf(stream, "%[^\n ] ", file_contents) != EOF)
   {
      if (compteur == 1)
      {
         tab_tags[tag_line].tagValue = atoi(file_contents); // value en décimal
      }
      if (compteur == 2)
      {
         strcpy(tab_tags[tag_line].tagHex, file_contents); // value en hexa
      }
      compteur++;

      if (compteur == 5)
      {
         tag_line++;
         strcpy(tab_tags[tag_line].tagName, file_contents); // deuxieme tag
         compteur = 1;
      }
   }
   tag_line++;
   fclose(stream);
   stream = 0;
}

/*-------------------------------MAIN-----------------------------------------*/
int
main(int argc, char** argv)
{
   char rom_path[256], bank_mapping_descr[128];
   unsigned char c, emu = 0;
   unsigned short old_pc;
   int b, s;
   int a = 0;
   unsigned short start;
   unsigned x;

   string[-1] = ' ';  /* that is ok! (prevents any label recognation in compile) */
   strcpy(rom_path, ".");
   strcpy(bank_mapping_descr, "");

   for (b = s = 1;s < argc && *argv[s] == '-';s++, b = 1)
   {
      if (!*(argv[s] + b))
         continue;
      else if (*(argv[s] + b) == 'E')
         emu = 1;
      else if (*(argv[s] + b) == 'B')
      {
         if (s + 1 >= argc || 1 != sscanf(argv[s + 1], "%127s", bank_mapping_descr))
            fprintf(stderr, "Error: option -B needs a filename argument\n");
         else
            b = 0, s++;
      }
      else if (*(argv[s] + b) == 'R')
      {
         if (s + 1 >= argc || 1 != sscanf(argv[s + 1], "%255s", rom_path))
            fprintf(stderr, "Error: option -R needs a path argument\n");
         else
            b = 0, s++;
      }
      else if (*(argv[s] + b) == 'h' || *(argv[s] + b) == '?')
      {
         printf("%s\n", msg1);
         printf("Usage: z80-mon [-h] [-E] [-B filename] [-R path] [<filename> ...]\n");
         return 0;
      }
   }
   clear_memory();
   MEMP = 0;

   for (;s < argc;s++)
   {
      for (b = 0;*(argv[s] + b) && *(argv[s] + b) != ':';b++);
      if (a = (*(argv[s] + b) == ':'))
         *(argv[s] + b) = '\0';

      stream = fopen(argv[s], "rb");

      if (!stream)
      {
         fprintf(stderr, "Error: Can't read file \"%s\".\n", argv[s]);
         return 1;
      }
      if (read_header(stream, &start, &x))
      {
         fprintf(stderr, "Error: \"%s\" is not a Z80 ASM file.\n", argv[s]);
         return 1;
      }
      if (a)
      {
         if (1 != sscanf(argv[s] + b + 1, "%hx", &start))
         {
            fprintf(stderr, "Error: \"%s\" is no hexadecimal address.\n", argv[s] + b + 1);
            return 2;
         }
      }
      dma_write(start, x, stream);
      MEMP = start;
      fclose(stream);
      stream = 0;
   }

   if (s == 1) // pas de paramètres
   {
      stream = fopen("new", "rb");

      if (!stream)
      {
         fprintf(stderr, "Error: Can't read file \"%s\".\n", argv[s]);
         return 1;
      }
      if (read_header(stream, &start, &x))
      {
         fprintf(stderr, "Error: \"%s\" is not a Z80 ASM file.\n", argv[s]);
         return 1;
      }

      dma_write(start, x, stream);
      MEMP = start;
      fclose(stream);
      /*
      stream = fopen("symbols", "r");
      if (!stream) { stream = 0;error_msg(load_name, "Can't read file");print_status();return 1; }
         load_symbols();// segmentation fault !!!
      */

      stream = 0;

   }

   c_init(BLACK); /** for DEBUG set it to WHITE **/
   c_cursor(C_HIDE);
   define_scroll_line(39, 23, 40);
   init_keyboard_map(KEYBOARD_MAP_FILE);

   if (init_ports())
      warning("asynchron buffered CPU port access impossible");
   else if (*bank_mapping_descr)
      init_banks(rom_path, bank_mapping_descr);
   if (emu)
   {
      init_cpu(_CPU);
      cpu_is_in_disassemble = 0;
      speed = 1 << 12;
      keyboard_disabled = 1;
      follow = 0;
   }
   else
   {
#ifdef Z80_CTC
      if (init_ctc())
         error_msg("asynchron CTC port access denied", "system:");
#endif
#ifdef LOGIC_ANALYZER
      reset_analyzer(".bus_proto");
#endif
      reset_cpu();
      follow = 1;
   }
   last_ticks = 0;
   io_address = NULL;

   disable_pseudo = 0;

   for (a = 0; a < MAX_BRK_PTS; a++)
   {
      breakpt[a] = 0;
   }
   init_interrupt_handling();
   print_panel();

   while (1)
   {
      if (cpu_pin[halt] && !IFF1)
      {
         if (!cpu_is_in_disassemble && emu)
         {
            dump_cpu(_CPU);
            break;
         }
         else
            cpu_is_in_disassemble = 1;
      }
      if (cpu_pin[busrq])
         /* don't run CPU */;
      else if (cpu_is_in_disassemble)
         usleep(20000);
      else
      {
 
         if (stream)protocol();

         decode(0, 1, 0);  /* here we decode and execute the current opcode */

         if (cpu_is_in_disassemble || speed <= 1) {
            print_panel();
            //c_getkey();
         }
         else if (speed == 4)
         {
            print_regs();print_ticks();print_halt();print_flags();
         }
         else
         {
            if (ticks >= last_ticks + speed)
               print_sp_and_pc(), print_ticks(), print_halt();
         }

         // Modification par Gilles POTTIER pour gérer plusieurs points d'arrêt
         for (a = 0; a < max_brk_pts;a++)
         {
            if (breakpt[a] >> 16) // = 1
            {
               if ((breakpt[a] & MAX_ADDR) == PC) // le point d'arret traité correspond à l'adresse en cours d'exécution
               {
                  // décremente le nombre de boucle demandées avant l'arrêt
                  breakpt[a] -= MAX_MEM;
                  print_breaks();
                  if (breakpt[a] >> 16){
                     //printf("breakpt[i] >> 16 = %ul", breakpt[a]);
                     cpu_is_in_disassemble = 1; 
                     //print_panel();
                     // Conserve le point d'arrêt
                     breakpt[a] += MAX_MEM;
                  }

                  break;
               }
            }
         }

      }
      if (cpu_pin[busrq])
         c = no_key_code; /* no keyboard input possible */
      else
      {
         if (cpu_is_in_disassemble)  keyboard_disabled = 0;
         c = !keyboard_disabled && c_kbhit() ? c_getkey() : no_key_code;
         if (cpu_is_in_disassemble && c != no_key_code)  set_cpu_pin(halt, 0);
      }
      if (c != no_key_code)
         //printf("code %c:", c);
         switch (c)
         {
         case '?':
         case 'h':
         case 'H':
            show_keys();
            print_panel();
            break;

            /* case 'q':  too dangerous */
         case 'Q': /* quit */
            c_shutdown();
            return 0;

         case 'x':
         case 'X':
            string[0] = 0;
            ask_str(string, "Execute instruction: ", 40);
            cpu_is_in_x_mode = 1;
            io_address = tmp_memory;
            set_cpu_pin(iorq, 1);
            old_pc = PC;
            set_start_address(0);
            disable_pseudo = 1;
            if (!compile(string - 1))
            {
               disable_pseudo = 0;
               PC = 0;
               decode(&old_pc, 1, 0);
            }
            else
               disable_pseudo = 0;
            set_cpu_pin(iorq, 0);
            io_address = NULL;
            cpu_is_in_x_mode = 0;
            print_panel();
            break;

         case ' ':  /* move to next instruction */
            decode(0, 2, 0);
            print_panel();
            break;

         case K_BACKSPACE:  /* exec one instruction */
            if (!cpu_is_in_disassemble) break;
            if (stream)protocol();
            decode(0, 1, 0);
            print_panel();
            break;

         case 'f': /* toggle flag */
         case 'F':
            ask_flag();
            print_panel();
            break;

         case '=':
            ask_register();
            break;

         case 's': /* change SP */
            x = ask_x("SP=", SP);
            if (x > MAX_ADDR) { c_bell();print_status();break; }
            cpu_is_in_disassemble = 1;
            SP = x;
            print_panel();
            break;

         case '+': /* speed */
            if (!speed)  speed = 1;
            else if (speed < 15000)  speed <<= 2;
            print_speed();
            break;

         case '-': /* speed */
            if (speed > 1)  speed >>= 2;
            else if (cpu_is_in_disassemble) speed = 0;
            print_speed();
            break;

         case 'r': /* toggle cpu_wait/run */
         case 'R':
            if (!cpu_pin[halt] || c == 'R') cpu_is_in_disassemble = !cpu_is_in_disassemble;
            if (!cpu_is_in_disassemble && !speed) speed = 1;
            print_panel();
            break;

         case 'i': /* change IM */
         case 'I':
            IM++;
            IM %= 3;
            print_panel();
            break;

         case 't': /* toggle disassembling numbers */
            /* 0:  default,  1: decimal, 2: hexadecimal, 3: hexadecimal with prefix */
            MODE = (MODE & ~3) | ((MODE & 3) + 1 & 3);
            print_panel();
            break;

         case 'j': /* toggle disassembling of address displacements (jr/djnz) */
            /* 0:  relative, 4: absolute 4 digit hexadecimal, 8,12: K-labels,V-labels */
            MODE = (MODE & ~12) | ((MODE & 12) + 4 & 12);
            print_panel();
            break;

         case '.': /* enter instruction */
            set_start_address(MEMP);
            do {
               sprintf(string, "0x%04x: ", MEMP);
               if (ask_str(string, "Put instruction into memory: ", 40))break;
               compile(string + 7);
               MEMP = get_current_address();
               print_panel();
            } while (string[0]);
            break;

         case K_ENTER: /* enter instruction */
            string[0] = 0;
            ask_str(string, "Put instruction at PC: ", 40);
            set_start_address(PC);
            compile(string - 1);
            print_panel();
            break;

         case '^': /* toggle EI/DI */
            IFF2 = IFF1;
            IFF1 ^= 1;
            print_panel();
            break;

         case '*':
            clear_user_regs();
            reset_banks();

         case '@':
#ifdef Z80_CTC
            reset_ctc();
#endif
            reset_cpu();
            last_ticks = 0;
            print_panel();
            break;

         case '#':
            clear_memory();
            print_panel();
            break;

         case '&': /* init stack_halt */
            stack_halt();
            print_panel();
            break;

         case '$':
            last_ticks = 0;
            set_tics(0);
            print_ticks();
            break;

         case 'p': /* ask about address */
         case 'P':
            x = ask_x("PC=", PC);

            if (x > MAX_ADDR) { c_bell();print_status();break; }
            cpu_is_in_disassemble = 1;
            PC = x;
            print_panel();
            break;

         case 'u':  /* defm */
         case 'U':
            sprintf(string, "0x%04x: defm \"", MEMP);
            if (ask_str(string + 13, string, 100))break;
            string[strlen(string)] = '"';
            set_start_address(MEMP);
            compile(string + 7);
            print_panel();
            break;

         case 'w':  /* defw */
         case 'W':
            sprintf(string, "0x%04x: defw ", MEMP);
            if (ask_str(string + 12, string, 64))break;
            set_start_address(MEMP);
            compile(string + 7);
            print_panel();
            break;

         case 'v':  /* defb */
         case 'V':
            sprintf(string, "0x%04x: defb ", MEMP);
            if (ask_str(string + 12, string, 64))break;
            set_start_address(MEMP);
            compile(string + 7);
            print_panel();
            break;

         case K_TAB:
            follow ^= 1;
            print_panel();
            break;

         case 'm': /* ask about start of memory dump */
         case 'M':
            follow = 0;
            x = ask_x("Enter memory address: ", MEMP);
            if (x > MAX_ADDR) { c_bell();break; }
            MEMP = x;
            print_panel();
            break;

         // Added by Gilles POTTIER
         case '8':
            follow = 0;
            x = MEMP - 1;
            if (x < 0) { c_bell();break; }
            if (x > MAX_ADDR) { c_bell();break; }
            MEMP = x;
            print_panel();
            break;

         case '2':
            follow = 0;
            x = MEMP + 1;
            if (x > MAX_ADDR) { c_bell();break; }
            MEMP = x;
            print_panel();
            break;

         case 'o': /* ask about start of memory screen dump */
         case 'O':
            follow = 0;
            x = ask_x("Enter memory screen address: ", MEMD);
            if (x > MAX_ADDR) { c_bell();break; }
            MEMD = x;
            print_panel();
            break;


         case 'L':   /* load */
            if (ask_str(load_name, "Load file: ", 40)) { c_bell();print_status();break; }
            stream = fopen(load_name, "rb");
            if (!stream) { stream = 0;error_msg(load_name, "Can't read file");print_status();break; }
            if (read_header(stream, &start, &x))
            {
               warning("Not a Z80 ASM file"); start = ask("Load to address: ", PC);
            }
            else
            {
               sprintf(string, "starting at 0x%x  %u bytes put", a, x); warning(string);
            }
            dma_write(start, x, stream);
            fclose(stream);
            stream = 0;
            print_panel();
            break;



         case 'Y':   /* load symbols table*/
            if (ask_str(load_name, "Load symbol table file: ", 40)) { c_bell();print_status();break; }
            stream = fopen(load_name, "r");
            if (!stream) { stream = 0;error_msg(load_name, "Can't read file");print_status();break; }

            load_symbols();
            print_panel();
            break;

         case 'S':   /* save */
            if (ask_str(save_name, "Save as: ", 40)) { c_bell();break; }
            x = ask("Length: ", 0);
            if (PC + x > MAX_MEM)break;
            stream = fopen(save_name, "wb");
            if (!stream) { stream = 0;error_msg(save_name, "Can't write to file ");print_status();break; }
            write_header(stream, PC);
            dma_read(PC, x, stream);
            fclose(stream);
            stream = 0;
            print_panel();
            break;

         case 'D': /* dump/disassemble */
            if (ask_str(save_name, "Disassemble to: ", 40)) { c_bell();break; }
            sprintf(string, "0x%04x - ", PC);

            x = ask_x(string, 0);
            if (x > MAX_MEM || x < PC)
            {
               error_msg("", "memory wrap_around");print_status();break;
            }
            stream = fopen(save_name, "a");
            if (!stream)
            {
               stream = 0;error_msg(save_name, "Can't append to file");print_status();break;
            }
            row = -2;
            if (MODE & 8)
            {
               hash_table_init();
               MODE |= 16;
            }
            old_pc = PC;
            while (PC <= x)
            {
               current_pc = PC;
               decode(0, 0, 0);
               if (!(MODE & 16))
                  fprintf(stream, "\n");
               if (PC < current_pc)  break;
            }
            PC = old_pc;
            if (MODE & 16)
            {
               char blanks[32];
               for (a = 0;a < 31;a++)
                  blanks[a] = ' ';
               blanks[a] = 0;
               MODE &= ~16;
               if (labels = table_entries())
               {
                  ele = malloc(labels * sizeof(struct info));
                  for (a = 0;next_table_entry(&ele[a].label, &ele[a].value, &ele[a].lineno);a++);
                  qsort(ele, labels, sizeof(struct info), compare_addr);
               }
               next_label_index = 0;
               old_pc = PC;
               fprintf(stream, "%sORG 0x%04x\n", blanks, old_pc);
               while (PC <= x)
               {
                  current_pc = PC;
                  decode(0, 0, 0);
                  fprintf(stream, "\n");
                  if (PC < current_pc)  break;
               }
               PC = old_pc;
            }
            if (MODE & 8)
            {
               char blanks[24];
               b = 0;
               for (a = 0;(unsigned)a < labels;a++)
                  if (ele[a].label[0]) b++;
               if (labels)
                  fprintf(stream, ";\n; %u Used Labels    %u Undefined Labels:\n", labels - b, b);
               if ((unsigned)b < labels)
               {
                  for (a = 0;a < 23;a++)
                     blanks[a] = ' ';
                  blanks[a] = 0;
               }
               for (a = 0;(unsigned)a < labels;a++)
                  if (ele[a].label[0])
                     fprintf(stream, "%s%5s   EQU  0x%04hx\n", blanks, ele[a].label, ele[a].value);
               free(ele);
               free_hash_table();
            }
            fclose(stream);
            stream = 0;
            print_panel();
            break;

         case '\"': /* protocol execution */
            if (ask_str(proto_name, "Protocol into: ", 40)) { c_bell();break; }
            stream = fopen(proto_name, "a");
            if (!stream) { stream = 0;error_msg(proto_name, "Can't append to file");print_status();break; }
            fprintf(stream, " PC  mnemonic     flags A  B  C  D  E  H  L "
               " A' B' C' D' E' H' L'  IX   IY   SP\n");
            print_status();
            break;

         case '!':  /* toggle keyboard interrupt */
            keyboard_disabled ^= 1;
            print_status();
            break;

         case K_ESCAPE:  /* NMI */
            nmi_handler();
            print_panel();
            break;

         case '%': /* ask about break point */
            a=0;

            // Ajout par Gilles POTTIER
            // Saisie du numéro du  breakpoint et de l'adresse du point d'arrêt <> PC
            x = ask_x("Enter breakpoint number (1-9): ", a);
            if (x > MAX_ADDR) { c_bell();break; }
            if (max_brk_pts < x)
            {
               max_brk_pts = x;
            }
            a = x - 1;

            _ushort bkp_addr;
            bkp_addr = ask_x("Enter breakpoint address: ", PC);
            if (bkp_addr > MAX_ADDR) { c_bell();break; }

            if (bkp_addr == 0)
            {
               breakpt[a] = 0;
               if (a == max_brk_pts)
               {
                  max_brk_pts--;
               }
            }
            else {
               MEMBKP = bkp_addr;
               if (MEMBKP == MAX_ADDR)
               {
                  MEMBKP = PC;
               }
               x=2;
               breakpt[a] = MEMBKP | x << 16;
            }

            print_breaks();
            break;

            PC = x;
            print_panel();
            break;

         default:
            c_bell();
            break;
         }
      if (cpu_is_in_disassemble)  emu = 0;  /* really finish emulation mode ?? */
      if (IFF0)
         IFF2 = IFF1 = 1;
      else if (cpu_pin[reset])
         reset_cpu();
      else if (cpu_pin[busrq])
         /* don't serve interrupts */;
      else if (IFF3)
         nmi_handler();
      else
         check_pending_interrupts();
   }
   c_shutdown();
   return 0;
}
