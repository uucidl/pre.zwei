#if defined(_MSC_VER)
#include "../modules/uu.spdr/src/spdr_win32_unit.c"
#elif defined(__APPLE__)
#include "../modules/uu.spdr/src/spdr_osx_unit.c"
#else
#include "../modules/uu.spdr/src/spdr_linux_unit.c"
#endif
