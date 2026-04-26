#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "log.h"

int main(int argc, char **argv)
{
    log_info("Hello IMX6ULL! Shadow Build Works!\n");
    printf("Hello IMX6ULL! Shadow Build Works!\n");
    return 0;
}