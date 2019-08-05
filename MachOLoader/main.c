#include <stdio.h>
#include <string.h>

#include "MachOLoader.h"

#define MAXARGS 2
#define AUTHOR "Brandon Dennis"
#define NAME "MachOLoader"

// Function Protos
void initCheck(int argc, char *argv[]);


int main(int argc, char *argv[]) {

    // Banner
    printf("\n\t\t%s\n\nAuthor: %s\n\n", NAME, AUTHOR);

    // Run Init arg check
    initCheck(argc, argv);

    LoadMachO(argv[1]);
    
    return 0;
}

void initCheck(int argc, char *argv[]) {
    // Check for Usage
    if (argc < MAXARGS || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-help") == 0) {
        // Print the usage for the application
        printf("\n\t\t%s\n\nAuthor: %s\n\n", NAME, AUTHOR);
        printf("Usage: %s <BinaryPathToMachO>\n\n", argv[0]);
    }
}