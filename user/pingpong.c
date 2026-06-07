#include "kernel/types.h"
#include "user/user.h"

int main(void) {
    int p[2], pid;
    char byte = 'h';
    if (pipe(p)) exit(1);

    if ((pid = fork()) < 0) exit(1);
    else if (pid == 0) {
        char c;
        read(p[0], &c, 1);
        printf("%d: received ping\n", getpid());
        write(p[1], &c, 1);
        exit(0);
    }
    write(p[1], &byte, 1);
    wait(0);
    read(p[0], &byte, 1);
    printf("%d: received pong\n", getpid());
    exit(0);
}

// #include "kernel/types.h"
// #include "user/user.h"
//
// int main(void) {
// int pp[2], pc[2], pid;
// char byte = 'h';
// if (pipe(pp) || pipe(pc)) exit(1);
//
// if ((pid = fork()) < 0) exit(1);
// else if (pid == 0) {
// close(pc[0]);
// close(pp[1]);
// if (read(pp[1], &byte, 1) != 1) exit(1);
// printf("%d: recieved ping\n", getpid());
// close(pp[1]);
// if (write(pc[0], &byte, 1) != 1) exit(1);
// close(pc[0]);
// exit(0);
// }
// close(pp[0]);
// close(pc[1]);
// if (write(pp[1], &byte, 1) != 1) exit(1);
// close(pp[1]);
// if (read(pc[0], &byte, 1) != 1) exit(1);
// printf("%d: recieved pong\n", getpid());
// exit(0);
// }
