#define LOCAL_HEADER "cpp78-has-include.h"
#define MISSING_HEADER "does-not-exist-cscout.h"
#define HEADER <stdio.h>

#if __has_include(HEADER)
# include HEADER
#endif

#if __has_include(LOCAL_HEADER)
has_local_header
#else
#error "failed to find macro-expanded quoted header"
#endif

#if __has_include(<stdio.h>) \
    && !__has_include(<does-not-exist-cscout.h>) \
    && !__has_include("does-not-exist-cscout.h") \
    && !__has_include(MISSING_HEADER)
has_system_header_only
#else
#error "incorrect missing or system header result"
#endif
