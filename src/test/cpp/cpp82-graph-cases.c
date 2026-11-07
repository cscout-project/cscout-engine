// Diverse call graph cases
#define M() A() + C() + X + R(F)
#define A() B()
#define B() K()
#define K() 1
#define C() 2
#define X Y
#define Y Z()
#define Z() 3
#define R(x) x()
#define F() 4

int i = M();
