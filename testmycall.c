/*---Start of C file ---*/
#include<stdio.h>
#include "testmycall.h"
int main(void)
{
printf("%d\n", syscall(__NR_mycall, 5636707));
}
/*---End of C file----*/
