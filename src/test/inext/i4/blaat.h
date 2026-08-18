
#warning "Dit is i4/blaat.h"

#if __has_include_next("blaat.h")
#error "__has_include_next found a header after the final include directory"
#endif

//#include_next "blaat.h"
