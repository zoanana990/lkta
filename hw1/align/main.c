#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "align.h"

#define EXPECT(x, expect) \
do{ \
   if(x != expect){ \
       printf("function execution error, x=%lu, expect=%d\n", (x), (expect));\
       abort(); \
   }\
}while(0)

int main()
{
    printf("Alignment test start !!\n");
    EXPECT(align_up(120, 4), 120);
    EXPECT(align_up(121, 4), 124);
    EXPECT(align_up(122, 4), 124);
    EXPECT(align_up(123, 4), 124);
    printf("Alignment test all pass!!\n");
    return 0; 
}
