/**
   NOTE(nicolas)

   We want to read from a root directory a series of small files in
   the fastest possible way.

   These files may either be stored locally or on a remote drive.

   We extract features from these files in memory.

   TODO(nicolas) find out if it's possible to read files locally and
   remotely while minimizing seeking. look for F_LOG2PHYS
*/

#define assert(condition, message)                                             \
        do {                                                                   \
                if (!(condition))                                       \
                        asm("int3");                                           \
        } while (0)

// this is the first version
int main(int argc, char **argv) { assert(false, "not implemented yet"); }
