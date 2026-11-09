/* Checks samp_auth_answer against every known pair in open.mp's table. */
#include <stdio.h>
#include <string.h>
#include "sampdata.h"
#include "authkey.h"
int main(void)
{
    int i, ok = 0;
    char a[41];
    for (i = 0; i < 256; i++) {
        if (samp_auth_answer((const unsigned char *)samp_auth[i].send, strlen(samp_auth[i].send), a) == 0 &&
            !strcmp(a, samp_auth[i].recv))
            ok++;
        else
            printf("MISMATCH %s -> %s want %s\n", samp_auth[i].send, a, samp_auth[i].recv);
    }
    printf("auth vectors ok: %d/256\n", ok);
    samp_auth_answer((const unsigned char *)"189C71CE27167", 13, a);
    printf("public sample: %s\n", a);
    return ok != 256;
}
