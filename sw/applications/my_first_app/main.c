#include <stdio.h>
#include <stdlib.h>

#include "core_v_mini_mcu.h"
#include "x-heep.h"

int main(int argc, char *argv[])
{
    printf("My first X-HEEP app!\n");
    printf("CPU frequency: %d Hz\n", REFERENCE_CLOCK_Hz);

    return EXIT_SUCCESS;
}
