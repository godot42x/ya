
#include <stdio.h>



typedef struct a
{
} a;

typedef struct b
{
} b;


int main()
{
    int arr[0];

    printf("%llu\n", sizeof(arr));
    // printf("%d\n", sizeof(void));
    printf("%llu\n", sizeof(a));
}