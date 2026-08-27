// Identify macros used in an integer constent expression context

// Macros that occur in contexts where an ICE is required
// This means they can't be converted into "const int", only
// to enumerator.
#define ENUM_INIT 1
#define CASE_LABEL 1
#define CASE_LABEL_FROM_EXPANSION 2
#define FIELD_WIDTH 1
#define STATIC_ASSERT 1
#define ARRAY_SIZE 1
#define ARRAY_DESIGNATOR 1

#define CASE_LABEL_2 CASE_LABEL_FROM_EXPANSION

#define OBJ_INIT 1
#define CPP_COND 1
#define PLAIN_ASSERT 1

// Macros that can't be replaced by C constants
#define HAS_STRING "foo"
#define UNBALANCED (

void assert(int);

void
a()
{
	enum { q = ENUM_INIT };
	switch (q) {
	case CASE_LABEL:
	case CASE_LABEL_2:
	case 3:
		;
	}
	_Static_assert(STATIC_ASSERT > 0, "...");
	int x[ARRAY_SIZE];
	const int k = OBJ_INIT;
	int a[] = { [ARRAY_DESIGNATOR] = 5 };
	assert(PLAIN_ASSERT > 3);
}
#if CPP_COND
#endif
