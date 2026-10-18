/*
 * Test macro call graphs:
 * Plain
 * To C function
 * Via C function
 * Outside C function
 * Via token pasting
 * via argument
 * Non-called
 * sandwitches
 * Via object-like macro
 */

#define top1() do { bottom1(); bottom1(); } while (0)
#define bottom1() 42

#define top2() middle2()
#define middle2() cbottom2()
void cbottom2(void) {}

#define top3() cvia3()
#define bottom3() 44
void cvia3(void) { bottom3(); }

#define top4() bottom4()
#define bottom4() 4
int k = top4();

#define top5(x, y) middle5(x ## y)
#define middle5(a) a()
#define bottom5() 5

#define top6(x, y) x()
#define bottom6() 6
#define noncalled6() 66

#define TOP7 top7()
#define top7() MIDDLE7
#define MIDDLE7 middle7()
#define middle7() bottom7()
#define bottom7() 7

#define top8(x, y, z) top_middle8(x, y, z)
#define top_middle8(x, y, z) x(z); y()
#define bottom_middle8(a) a()
#define bottom_middle82() 82
#define bottom8() 8

#define top9() MIDDLE9a
#define MIDDLE9a MIDDLE9b
#define MIDDLE9b bottom9()
#define bottom9() 9

int
main()
{
	top1();
	top2();
	top3();
	top5(bottom, 5);
	top6(bottom6, noncalled6);
	TOP7;
	top8(bottom_middle8, bottom_middle82, bottom8);
	top9();
}
