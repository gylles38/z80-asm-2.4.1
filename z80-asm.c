#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hash.h"
#include "asm.h"
#include "file.h"

static FILE *input;

static unsigned char memory[MAX_MEM];
// Specific for Newbrain fomat (reversed datas)
static unsigned char revmemory[MAX_MEM];
static unsigned short start,length, datalength;

unsigned char
write_to_memory(unsigned short index, unsigned char a)
{
 unsigned char previous=memory[index];
 memory[index]=a;
 return previous;
}

// For Newbrain tape
char spaces[10]; // 10x0 + \n
//

static int
take_line(char *line, int max_chars)
{
 if (!fgets(line,max_chars-1,input)){line[0]=0;return 1;}  /* end of file */
#ifdef UNIX
 if (strlen(line) && line[strlen(line)-1]=='\n')
#endif
 line[strlen(line)-1]=0;
#ifdef UNIX  /* reading DOS-files */
 if (strlen(line) && line[strlen(line)-1]=='\r')
    line[strlen(line)-1]=0;
#endif
#ifdef DOS
 if (!strlen(line))take_line(line,max_chars);
#endif
 return 0;
}


static void
usage(char *myname)
{
printf(
"Z80 assembler V 2.4.1 "
"(c)1999-2004 Brainsoft  (Copyleft) 1999-2018\n"
"Usage: %s [-w] [-h] [-l] [-f xx] [-c] <input file .asm> [<start address>[[:<length>]:<output file>]]\n"
"Usage: %s [-w] [-h] [-l] [-f xx] [-c] <input file> [<start address>[:<length>]:<output file>] ...\n"
"Usage: %s [-z] Newbrain binay file format\n"
,myname,myname);
}


void
error(int l,char *line,char *txt)
{
fprintf(stderr,"%s\nline %d: %s\n",line,l,txt);
}


/* return value: 0=OK, 1=syntax error */
/* parses command line options */

static int
parse_arg(char *arg,char **filename)
{
   unsigned long a;
   char *p, *q, *e;
 
   q=arg;
   if (!*q && !*filename)
   {  fprintf(stderr,"Error: Output filename not specified.\n");
      return 1;
   }
   for (p=arg ; (*p)&&(*p)!=':' ; p++);  /* find ':' or end of string */
   if (*p)
   {  *p=0;
      p++;
      a=strtoul(arg,&e,16);
      if (*e)
      {  fprintf(stderr,"Error: Starting address is not a number.\n");
         return 1;
      }
      if (a >= 1<<16)
      {  fprintf(stderr,"Error: Starting address out of range.\n");
         return 1;
      }
      start=(unsigned short)a;
      q=p;
   }
   else
      start=0;
   for (; (*p)&&(*p)!=':' ; p++);  /* find ':' or end of string */
   if (*p) /* start:length:filename */
   {
      *p=0;
      p++;
      a=strtoul(q,&e,10); 
      if (*e)
      {  fprintf(stderr,"Error: Length is not a number.\n");
         return 1;
      }
      if (a>=1<<16)
      {  fprintf(stderr,"Error: Length out of range.\n");
         return 1;
      }
      length=(unsigned short)a;
      if ((int)start+length >= 1<<16)
      {  fprintf(stderr,"Error: File is too long.\n");
         return 1;
      }
      q=p;
   }
   else
      length=0;  /* start:filename */
   if (*q)
      *filename=q;
   return 0;
}


struct info{ char *label; int value; unsigned lineno; };

static int compare(const struct info *left, const struct info *right)
{
   return  strcmp(left->label,right->label);
}   

int
main(int argc, char **argv)
{
FILE *output;
char *txt=0;
char line[512];
int s,a=0,b,cross=0;
/* a is used as memory init value */

for (b=s=1;s<argc&&*argv[s]=='-';b++)
{
   if (!*(argv[s]+b))
      b=0, s++;
   else if (*(argv[s]+b)=='w')
   {  printf("Warnings switched on.\n");WARNINGS=1;
   }
   else if (*(argv[s]+b)=='z') Z80=1;
   else if (*(argv[s]+b)=='l') LISTING=1;
   else if (*(argv[s]+b)=='c') cross=1;
   else if (*(argv[s]+b)=='f')
   {  if (s+1>=argc || 1!=sscanf(argv[++s],"%2x",&a))
         fprintf(stderr,"Error: option -f needs a hexadecimal argument\n");
      else
         b=0, s++;
   }
   else if (*(argv[s]+b)=='h') {usage(argv[0]);}
   else if (*(argv[s]+b)=='?') {usage(argv[0]);}
   else if (*(argv[s]+b)=='-') {s++;break;} /* in case filename equals option */
   else fprintf(stderr,"Error: unknown option -%s\n",argv[s]+b);
}

if (s == argc) return 0;
input=fopen(argv[s],"r");
if (!input)
{fprintf(stderr,"Error: can't open input file \"%s\".\n",argv[s]);return 1;}

disable_pseudo=0;
memset(memory,a,1<<16);
asm_init((unsigned char)a);

a=0;
set_compile_pass(1);
set_start_address(0);

while (!a && !take_line(line,511))
{
  a= compile(line);
}

if (a==8) a=0;
if (!a)
  a=check_cond_nesting();
if (!a)
 {
 if (fseek(input,0,SEEK_SET))
 {asm_close();fprintf(stderr,"can't rewind input file \"%s\".\n",argv[s]);return 2;}
 set_compile_pass(2);
 set_start_address(0);

 while (!a && !take_line(line,511)) 
   {
      a= compile(line);
   }
 }

if (a==8) a=0;
fclose(input);
if (s+1 == argc)
{asm_close();fprintf(stderr,"No code generated\n");return 0;}
if (!a)
{
   for (b=1+s;b<argc;b++)
   {
      txt=0;
      if (argc==2+s && strlen(argv[s]) >= 5 &&
          !strcmp(argv[s]+strlen(argv[s])-4,".asm"))
      {  txt=argv[s];
         sprintf(txt+strlen(argv[s])-3,"z80");
      }
      if (parse_arg(argv[b],&txt))
      {  asm_close();  return 1;  }
      output=fopen(txt,"wb");
      if (!output)
      {  asm_close();
         fprintf(stderr,"Error: Can't open output file \"%s\".\n",txt);
         return 1;
      }
      if (!Z80){
         write_header(output,start);
      }
      else {
         datalength = length?length:highest_address()+1-start;

         unsigned char title[256];
         //out((highest_address() - lowest_address() + 1) >> 8);
         //out((highest_address() - lowest_address() + 1) & 255);

//printf("%u", (highest_address() - lowest_address() + 1));return 1; // 28

         //printf("%d ", (highest_address() - lowest_address() + 1) >> 8);
         //printf("%d ", (highest_address() - lowest_address() + 1) & 255);
      }

      if (length || generated_bytes() && highest_address() >= start) {
         if (!Z80){
            fwrite(memory+start,1,length?length:highest_address()+1-start,output);
         }
         else {
            // Newbrain format - reverse data memory

            // 3 caractères :  0 tailletitre 0 + strlen(titre) + 81(H) + 84(H) + 2 (nbblocs?) + 10
            // Création du bloc titre
            write_8bits(output, 0, 1);

            // si > 0 titre
            write_8bits(output, strlen("TEST    "), 2);
            write_string(output, "TEST    \n");

            write_8bits(output, 0x81, 1); // toujours 81(H)

            write_8bits(output, 0x84, 1); // comment est-ce calculé ? taille du buffer ?

            write_8bits(output, 0x2, 1); // comment est-ce calculé ? nombre de blocs ?

            // 10 prochains éléments sont à 0 par défaut
            memset(spaces, 0, 10);
            fwrite(spaces,1, sizeof(spaces) ,output);

            // Longueur des datas + ?
            // printf("%d", highest_address() - lowest_address() + 1); 27 datas + 2 = 29 + 2 = 31 + 2 = 33 + 1(01) = 34 - 22(H)
            int size = highest_address() - lowest_address() + 1 + 2 + 2 + 2 + 1; 
            write_8bits(output, size, 2);

            // Valeur de la directive ORG en entête du code source
            write_16bits(output, lowest_address());
            //revmemory[bloctitre+3] = 62; // 3E
            //revmemory[bloctitre+4] = 123; // 7B 

            // datas reverse
            for (int boucle=datalength-1;boucle >= lowest_address(); boucle--) {
               revmemory[datalength - boucle - 1] = memory[boucle];
            }
            // Ecriture des datas
           fwrite(revmemory+start,1, highest_address() - lowest_address() + 1 ,output);

            // Taille des données utiles du bloc
            write_16bits(output, (highest_address() - lowest_address() + 1)); // 0 1B - 27

            // Valeur de la directive ORG
            write_16bits(output, lowest_address()); // 3E 7B


// E1 DD E1 FD 31     ...  3E 7B  21 E5  FD  E5  DD  0 42 0 41 0 1B 3E 7B  01 41 8E  0E
// 225 221 225 253 49 ...  62 123 33 229 253 229 221 0 66 0 65 0 27 62 123 1  65 142 14
            // Numéro du bloc ?
            write_8bits(output, 1, 1); // 01

            // Quoi mettre exactement surtout pour les 2 derniers bytes qui varie en fonction des données du code
            // 41(H) 8E(H) 0E(H) - 65 142 14 // 41 59 0F - 65 89 15 // 41 23 10 - 65 35 16
            // ORG écart de 15 entre 15995 et 16000, écart de 3 entre 16000 et 16001
            // Si ORG 0 => 0E(H) 14(D), si ORG 1 => 11(H) 17(D) soit un écart de 3, ORG 2 => 14(H) 20(D) soit un écart de 3, ...

            // Formule a appliquer en conservant le poids fort (2eme byte du résultat)
            // valeur du ORG * 3 + 14 = 

            write_8bits(output, 0x41, 1); // toujours 41(H) ?

            // 15995 => 58(H) - 88(D) //  16000 => 67(H) - 103(D) // 16001 => 6A(H) - 106(D)
            // 1 > 86   134
            // 255 > 82 130
            // 256 > 86 134
            // 510(2x255) > 82 130 
            // 511(256+255) > 84 132
            // 512(2x256) > 88 136
            // 765(3x255) > 82 130
            // 768(3x256) > 8A 138
            write_8bits(output, 0x8E, 1); // Kesako ??? 58(H) pour ORG 15995 - 67(H) pour 16000 soit un écart de 15
            
            write_8bits(output, 0x0E, 1); // Nombre de lignes de codes (de ORG inclus jusqu'au END exclus) => TODO  - A CALCULER

            // 9 blocs à 0 - on tente sans pour voir
//            fwrite(newbrain_title.spaces,1, sizeof(newbrain_title.spaces) -1 ,output);

         }
      }
      fclose(output);
   }
}
if (!a && (LISTING||cross))
   printf("%u bytes code generated and %u labels defined\n",
          generated_bytes(),table_entries());
if (!a && cross && (b=table_entries()))
{ struct info  *ele;
  ele= malloc(b*sizeof(struct info));
  for(a=0;next_table_entry(&ele[a].label,&ele[a].value,&ele[a].lineno);a++);
  qsort(ele,b,sizeof(struct info),compare);
  printf("       Cross reference:\n");
  printf("      symbol value hexa  line\n");
  for (a=0;a<b;a++)
   printf("%12.12s %5d %4hx %5u\n",ele[a].label,ele[a].value,
          (unsigned short)ele[a].value,ele[a].lineno);
  free(ele);
  a=0;
}
asm_close();
return a;
}
