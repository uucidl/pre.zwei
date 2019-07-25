#if !defined(ZWEI_FILES_PLATFORM_PROTOTYPES)
#define ZWEI_FILES_PLATFORM_PROTOTYPES

void unix_advise_sequential_readonce(int fd);

#endif

#if defined(ZWEI_FILES_PLATFORM_IMPLEMENTATION)

#if defined(__APPLE__) // TAG(platform_specific) TAG(osx)
#include <fcntl.h>

void unix_advise_sequential_readonce(int fd) { fcntl(entry_fd, F_RDAHEAD, 1); }

#elif _POSIX_C_SOURCE >= 20112L
#include <fcntl.h>

void unix_advise_sequential_readonce(int fd)
{
        (void)posix_fadvise(fd, 0, 0,
                            POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);
}
#endif

#endif
