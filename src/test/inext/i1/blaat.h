
#warning "Dit is i1/blaat.h"

#if !__has_include_next("blaat.h")
#error "__has_include_next failed to find i2/blaat.h"
#endif

#include_next "blaat.h"
