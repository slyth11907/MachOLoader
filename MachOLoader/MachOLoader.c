#include "MachOLoader.h"

#define INCR 0x1000
#define EXECUTABLEBASE 0x100000000
#define DYLDBASE 0x00007fff5fc00000

// Globals
bool isSierra = false;

int LoadMachO(char *fileName) {

    // Run an inital check to see if we re on Sierra or not
    checkSierra();

    unsigned long tmp, dlyd;

    // Check if we are using sierra
    // We need to locate dyld in memory.
    if (isSierra) {
        // Run based on sierra
        // Check if we can see the first loaded binary and then the dlyd binary
        if (ScanMemoryForMachoHeader(EXECUTABLEBASE, &tmp, INCR, 0) == 1 || ScanMemoryForMachoHeader(tmp + INCR, &dlyd, INCR, 0) == 1){
            return 1;
        }

    } else {
        // Run based on older version of MacOSX
        // Check if we can see the dlyd base
        if (ScanMemoryForMachoHeader(DYLDBASE, &dlyd, INCR, 0) == 1) {
            return 1;
        }
    }

    //Load File into Memory Via Socket
    // TODO REPLACE
    char *bin = NULL;
    unsigned int size;
    if (load_from_disk(fileName, &bin, &size)) {
        printf("[ERROR]: Failed to Write to memory of the bin\n");
        exit(1);
    }

    // Get memory location for NSCreateFileImageFromMemory
    unsigned long NSCFIFM = get_NSCFIFM(dlyd);

    // Get memory location for NSLinkModule
    unsigned long NSLM = get_NSLM(dlyd);


    // Push to Memory
    LoadMachOToMemory(NSCFIFM, NSLM, bin, size);

    return 0;

}

void LoadMachOToMemory(unsigned long NSCFIFM, unsigned long NSLM, char *bin, unsigned int size) {

    // Creating initial Func Map for NSCreateFileImageFromMemory & NSLinkModule
    NSObjectFileImageReturnCode(*NSCreateFileImageFromMemory)(const void *, size_t, NSObjectFileImage *) = NULL;
    NSModule(*NSLinkModule)(NSObjectFileImage, const char *, unsigned long) = NULL;

    // Build Function from memory location
    NSCreateFileImageFromMemory = (NSObjectFileImageReturnCode(*)(const void *, size_t, NSObjectFileImage *))NSCFIFM;
    NSLinkModule = (NSModule(*)(NSObjectFileImage, const char *, unsigned long))NSLM;

    // Change the filetype from Ox2(MH_EXECUTE) -> Ox8(MH_BUNDLE) to be a bundle 
    if (((int *)bin)[3] != MH_BUNDLE) {
        // Swap the bytes to be a bundle
        ((int *)bin)[3] = MH_BUNDLE;
    }

    // Create our file Image
    NSObjectFileImage fileImg;

    // Run NSCreateFileImageFromMemory to create the file image
    if(NSCreateFileImageFromMemory(bin, size, &fileImg) != 1) {
        // Fail here as we cannot create the image
        printf("[ERROR]: Failed to create File Image From Memory.\n");
        exit(1);
    }

    // Run NSLinkModule to link the fake module to our file image
    // Generate a random string for our module name
    char *randMod[10];
    gen_random(randMod, 10);
 
    // Link our random valued module
    NSModule linkedModule = NSLinkModule(fileImg, randMod, NSLINKMODULE_OPTION_PRIVATE | NSLINKMODULE_OPTION_BINDNOW);

    // Check to see if we actually linked our new module
    if (!linkedModule) {
        // We failed exit
        printf("[ERROR]: Failed to Link new Module!\n");
        exit(1);
    }

    // Init our base execution addr & entr_point_command struct
    unsigned long executeBase;
    struct entry_point_command *entryPC = NULL;


    // Find the MachO Header of our Linked Module
    if (ScanMemoryForMachoHeader((unsigned long)linkedModule, &executeBase, sizeof(int), 1)) {
        printf("[ERROR]: Failed to find MachO Header for Linked Module\n");
        exit(1);
    }

    // Find the entry_point_command structure in the linked module MachO
    if (find_entry_point(executeBase, &entryPC)) {
         printf("[ERROR]: Failed to find entry_point_command in MachO Header for Linked Module\n");
        exit(1);
    }

    // Preconfigure a start method to launch it
	int(*start)(int, char**, char**) = (int(*)(int, char**, char**))(executeBase + entryPC->entryoff); 

    // Launch the Binary from Memory
	char *argv[]={NULL, NULL};
	int argc = 1;
	char *env[] = {NULL};
	start(argc, argv, env);
}

int find_entry_point(unsigned long addr, struct entry_point_command **entryPC) {

    // Set our machO Header
    struct mach_header_64 *header = (struct mach_header_64 *)addr;

    // Grab our first Load Command
    struct load_command *LoadCMD = (struct load_command *)(addr + sizeof(struct mach_header_64));

    // Loop through our ncmds and look for LC_MAIN and set entryPC to it
    for (int i = 0; i < header->ncmds; i++) {
        // Check if we are at LC_MAIN
        if (LoadCMD->cmd == LC_MAIN) {
            // Set our entryPC to this Load Command
            *entryPC = (struct entry_point_command *)LoadCMD;
            return 0;
        }

        // Set our Load Command to the next one in line
        LoadCMD = (struct load_command *)((unsigned long)LoadCMD + LoadCMD->cmdsize);
    }

    return 1;

}

// Get memory location for NSCreateFileImageFromMemory
unsigned long get_NSCFIFM(unsigned long dlyd) {

    // Resolve the Symbols for NSCreateFileImageFromMemory
    return grab_Symbol(dlyd, "_NSCreateObjectFileImageFromMemory");
}

// Get memory location for NSLinkModule
unsigned long get_NSLM(unsigned long dlyd) {

    // Resolve the Symbols for NSLinkModule
    return grab_Symbol(dlyd, "_NSLinkModule");
}

// Grab the symbols memory addr based on offset / pair
unsigned long grab_Symbol(unsigned long addr, char *funcName) {

    // Create initial structs for the MachO sections
    struct load_command *loadCMD;
    struct symtab_command *symCMD;
    struct segment_command_64 *linked, *textSection, *segCMD;
    struct nlist_64 *nlist64;


    // Load up our first load_command and skip the machO header
    loadCMD = (struct load_command *)(addr + sizeof(struct mach_header_64));

    // Loop through all of the ncmds and find the symtab & Link + Text section
    for (int i = 0; i < ((struct mach_header_64 *)addr)->ncmds; i++) {

        // Check to see if we have a LC_SYMTAB
        if (loadCMD->cmd == 0x2) {
            // set our SYMCMD var to the new memory location
            symCMD = (struct symtab_command *)loadCMD;

            // check for LC_SEGMENT_64
        } else if (loadCMD->cmd == 0x19) {
            // Load in the segment 64 struct
            segCMD = (struct segment_command_64 *)loadCMD;

            // Check if we have the text segment
            if (*((unsigned int *)&((struct segment_command_64 *)loadCMD)->segname[2]) == 0x54584554) { // hex is TEXT reversed
                // Keep track of the memory location of the section
                textSection = segCMD;
            }

            // Check if we have the linkedit segment
            if (*((unsigned int *)&((struct segment_command_64 *)loadCMD)->segname[2]) == 0x4b4e494c) { // hex is LINK reversed
                // Keep track of the memory location of the section
                linked = segCMD;
            }
        }

        // reset our Load Command based on the cmdsize offset
        loadCMD = (struct load_command *)((unsigned long)loadCMD + loadCMD->cmdsize);
    }

    // Check to see if we have all of our sections set if not exit
    if (textSection == NULL || linked == NULL || symCMD == NULL) {
        printf("[ERROR]: Failed to find all Sections!\n");
        exit(1);
    }

    // We need to calc the offset of the strings table
    unsigned long stringsOffset = linked->vmaddr - textSection->vmaddr - linked->fileoff;

    // Build the string tables IP
    char *stringTable = (char *)(addr + stringsOffset + symCMD->stroff);

    // We now grab the first nlist bsed on the strings table offset
    // nlist is an entry in the strings table
    nlist64 = (struct nlist_64 *)(addr + stringsOffset + symCMD->symoff);

    // Loop through each nlist in the symbols table
    for (int i = 0; i < symCMD->nsyms; i++){

        // Grab the name of the current symbol entry based on its index of n_un
        char *symbName = stringTable + nlist64[i].n_un.n_strx;

        if (!strcmp(symbName, funcName)) {

            // Check if we are using Sierra
            if (isSierra) {
                //Return the base  + the n_value of the symbols table
                return addr + nlist64[i].n_value;
            } else {
                // Return the main address - the DLYD Base address + the n_value of the symbols table
                return addr - DYLDBASE + nlist64[i].n_value;
            }
        }
    }

    // If we hit here we failed so exit
    printf("[ERROR]: Failed to find %s!\n", funcName);
    exit(1);

}

// Check to see if we will be running from a sierra machine or not
void checkSierra() {

    // Check to see if we are in Sierra with the /bin/rcp binary
    // This was removed after Sierra
    struct stat buf;

    isSierra = (bool) stat("/bin/rcp", &buf);

}

int ScanMemoryForMachoHeader(unsigned long startAddr, unsigned long *foundAddr, unsigned int incr, unsigned int deref) {

    // Reset our found to 0  to get an even base line
    *foundAddr = 0;

    // Inital PTR
    unsigned long ptr;

    // Loop Through
    while (1) {

        // update our ptr
        ptr = startAddr;

        // Dereference if we need to
        if(deref == 1) ptr = *(unsigned long *)ptr;

        // Loop through every memory address with the incr offset
        // Use chmod to see if we are in memory space we have access to by looking for ENOENT(2) error
        // Chmod will return EFAULT if we are outside of our memory location
        chmod((char *)ptr, 0777);

        // Check for our error
        // Hitting [0] is the value at the memory location
        if (errno == 2 && ((int *)ptr)[0] == MH_MAGIC_64) {
            *foundAddr = ptr;
            return 0;
        }

        startAddr += incr;
    }

    return 1;

}

// Generate a random set of chars based on the length provide
void gen_random(char *s, const int len) {
    static const char alphanum[] =     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    s[len] = 0;
}


/// REPLACE!!!!!!
int load_from_disk(char *filename, char **buf, unsigned int *size) {

	int fd;
	struct stat s;

	if((fd = open(filename, O_RDONLY)) == -1) return 1;
	if(fstat(fd, &s)) return 1;
	
	*size = s.st_size;

	if((*buf = mmap(NULL, (*size) * sizeof(char), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANON, -1, 0)) == MAP_FAILED) return 1;
	if(read(fd, *buf, *size * sizeof(char)) != *size) {
		free(*buf);
		*buf = NULL;
		return 1;
	}

	close(fd);

	return 0;
}
