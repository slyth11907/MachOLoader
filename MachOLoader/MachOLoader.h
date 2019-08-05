#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include <mach-o/nlist.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>


// Protos
int LoadMachO(char *fileName);
void checkSierra();
int ScanMemoryForMachoHeader(unsigned long startAddr, unsigned long *foundAddr, unsigned int incr, unsigned int deref);
unsigned long get_NSCFIFM(unsigned long dlyd);
unsigned long grab_Symbol(unsigned long addr, char *funcName);
unsigned long get_NSLM(unsigned long dlyd);
void LoadMachOToMemory(unsigned long NSCFIFM, unsigned long NSLM, char *bin, unsigned int size);
int load_from_disk(char *filename, char **buf, unsigned int *size);
void gen_random(char *s, const int len);
int find_entry_point(unsigned long addr, struct entry_point_command **entryPC);