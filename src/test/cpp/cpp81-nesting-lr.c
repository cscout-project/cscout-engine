// Test call graph
// A() calls B() changing M()'s context, but then M() also calls C().

#define M() A() + C()
#define A() B()
#define B() 1
#define C() 2

int i = M();
