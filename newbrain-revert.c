#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hash.h"

static FILE* input;

unsigned char memory[MAX_MEM];

// Convert PC source assembler to Newbrain Emulator format
int
main(int argc, char** argv)
{
    int s, a = 0, b;
    FILE* output;

    for (b = s = 1;s < argc && *argv[s] == '-';b++)
    {
        if (!*(argv[s] + b))
            b = 0, s++;
    }

    if (s == argc) return 0;

    input = fopen(argv[s], "rb");
    if (!input)
    {
        fprintf(stderr, "Error: can't open input file \"%s\".\n", argv[s]);return 1;
    }

    memset(memory, a, 1 << 16);

    a = 0;

    int index = 4; // Index 0 à 3 réservés pour la mise à jour après calcul des databytes
    int dataLength = 0;
    int checksum = 0;
    bool newLine = 0;

    //bool newline = 0;
    int line = 1;

    while ((a = fgetc(input)) != EOF) {

        if (a == 0x0A || index == 4) {
            // newline
            if (a == 0x0A) {
                memory[index] = 0x0D; // Newbrain EOL
                checksum += memory[index];
                printf("<%02x>", memory[index]);
                index++;
                dataLength++;
                printf("\ndataLength = %d %02x\nNewline :", dataLength, dataLength);
            }
            else {
                memory[index] = 0x13; // 1ère ligne, insérer 13 au début
                checksum += memory[index];
                printf("%02x **", memory[index]);
                index++;
                printf("\n%d %02x\n1rst line :", dataLength, dataLength);
            }
            memory[index] = line; // No ligne (2 bytes)
            checksum += memory[index];
            printf("<%02x>", memory[index]);
            index++;

            memory[index] = 0x00;
            printf("<%02x>", memory[index]);
            index++;

            memory[index] = 0x20; // espace
            checksum += memory[index];
            printf("<%02x>", memory[index]);
            dataLength += 3;
            line += 10;
            newLine = 1;
        }
        else {
            if (newLine == 1) {
                newLine = 0;
                if (a == 0x20) {
                    // Il  faut supprimer l'espace
                    printf(" rspace ");
                    index--;
                }
                else {
                    memory[index] = a;
                    checksum += memory[index];
                    printf("<%02x>", memory[index]);
                    dataLength++;
                }
            }
            else {
                memory[index] = a;
                checksum += memory[index];
                printf("<%02x>", memory[index]);
                dataLength++;
            }
        }
        index++;
    }

    memory[index] = 0x0D; // Newbrain EOL
    checksum += memory[index];
    printf("<<%02x>>", memory[index]);
    dataLength++;
    index++;

    memory[index] = 0x04; // Databytes end ? Why 04H or 45H ?
    checksum += memory[index];
    printf("\n%02x ", memory[index]);
    index++;

    memory[index]=  (dataLength + 3) >> 8; // Data length low byte
    checksum += memory[index];
    index++;
    memory[index]=  (dataLength + 3) & 0xFF; // Data length high byte
    checksum += memory[index];

    printf("\nTotal databytes = %d %04x\n", dataLength, dataLength);
    printf("Index-4=%02x\n", index-4);

    memory[3] = dataLength & 0xFF; // Databytes length
    checksum += memory[3];
    memory[2] = 0x41; // Always ? Why 41H ?
    checksum += memory[2];

    checksum += 0x3B; // Offset
    memory[0]=  checksum >> 8; // Data length low byte
    memory[1]=  checksum & 0xFF; // Data length high byte
    printf("\n%02x\n", checksum);

    output = fopen("source  .bas", "wb");

    // Création du bloc de titre
    fputc(0x0, output);
    fputc(0x8, output); // Le titre fera toujours 8 caractères au besoin complété par des espaces(20H)
    fputc(0x0, output);

    char title[9];
    strcpy(title, "source  ");
    fprintf(output, title);

    fputc(0x81, output); // Always 81H ? Why ?

    fputc(0x95, output); // why ? Lié au titre du fichier // a=>05 02 b=>06 02 h=>0C 02... aa=>46 02(+41) aaa=>87 02(+41) bb=>48 02(+42)
    fputc(0x03, output); // why ?

    char spaces[10];
    memset(spaces, 0, 10);
    fwrite(spaces,1, sizeof(spaces) ,output);
    
    // Numéro de ligne sur 5 digits (ex 00020 => 00 14)
    for (int boucle = index;boucle >= 0; boucle--) {
        fputc(memory[boucle], output);
    }

    fclose(input);
    fclose(output);
}
