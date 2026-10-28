// Most builtin operators are considered defined
#if defined(__has_include_next)
        int OK___has_include_next;
#else
        int FAILURE___has_include_next;
#endif

#ifdef __has_include
        int OK___has_include;
#else
        int FAILURE___has_include;
#endif

// But not defined
#if defined(defined)
        int FAILURE_defined;
#else
        int OK_defined;
#endif
