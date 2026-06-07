#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc != 2)
        fprintf(2, "%s takes 1 arguments: %d recieved\n", argv[0], argc - 1), exit(1);

    int t = atoi(argv[1]);

    sleep(t);

    exit(0);
}
