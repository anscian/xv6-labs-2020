#include "kernel/types.h"
#include "user/user.h"

void sieve(int p[2]);

int main(void) {
    int p[2], pid, i;
    pipe(p);

    if ((pid = fork()) < 0) exit(1);
    else if (pid == 0)
        sieve(p);

    close(p[0]);
    for (i = 2; i < 36; i++)
        write(p[1], (char *) &i, 4);
    close(p[1]);
    wait(0);
    exit(0);
}

void sieve(int p[2]) {
    int rd, n, pid, i;
loop:
    close(p[1]);
    rd = p[0];
    if (read(rd, (char *) &n, 4) == 0)
        exit(0);
    printf("prime %d\n", n);

    pipe(p);
    if ((pid = fork()) < 0) exit(1);
    else if (pid == 0)
        goto loop;

    close(p[0]);
    while (read(rd, (char *) &i, 4))
        if (i % n)
            write(p[1], (char *) &i, 4);
    close(rd);
    close(p[1]);
    wait(0);
    exit(0);
}
