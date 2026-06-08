#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char *arguments[argc], buf[512], *p;

    for (int i = 1; i < argc; i++)
        arguments[i - 1] = argv[i];
    arguments[argc - 1] = buf;

    p = buf;
    while (1) {
        if (read(0, p, 1) == 0) {
            if (p == buf) break;
            *p = 0;
            if (fork() == 0)
                exec(arguments[0], arguments);
            wait(0);
            break;
        }
        else if (*p == '\n') {
            if (p == buf) continue;
            *p = 0;
            if (fork() == 0)
                exec(arguments[0], arguments);
            wait(0);
            p = buf;
        }
        else if (p - buf > sizeof buf - 2) {
            *p = 0;
            fprintf(2, "xargs: argument too long (%s), skipped\n", buf);
            while (read(0, p, 1) > 0 && *p != '\n') ;
            p = buf;
        }
        else p++;
    }

    exit(0);
}
