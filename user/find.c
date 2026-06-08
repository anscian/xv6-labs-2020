#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

void find(char *path, char *target) {
    int fd;
    struct dirent de;
    struct stat   st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (st.type == T_FILE) {
        char *p = path + strlen(path) - 1;
        while (p >= path && *p != '/') p--;
        ++p;

        if (strcmp(p, target) == 0)
            printf("%s\n", path);
    }
    else if (st.type == T_DIR) {
        char buf[512], *p;
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof de) == sizeof de) {
            if (de.inum == 0)
                continue;
            if (!strcmp(de.name, ".") || !strcmp(de.name, ".."))
                continue;
            strcpy(p, de.name);

            find(buf, target);
        }
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if (--argc != 2)
        fprintf(2, "find expects 2 arguments: %d were given\n", argc),
        exit(1);

    find(argv[1], argv[2]);
    exit(0);
}
