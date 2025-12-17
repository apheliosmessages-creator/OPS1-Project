#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "commands.h"

int main(void) {
    char line[1024];

    printf("Commands: add <src> <dst>, end <src> <dst>, list, exit\n");

    while (1) {
        printf("> ");
        fflush(stdout);
    
        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\n")] = 0;

        wordexp_t p;
        int ret = parse_command(line, &p);
        if (ret != 0) {
            printf("Parse error: %d on line: %s\n", ret, line);
            continue;
        }

        if (p.we_wordc == 0) {
            wordfree(&p);
            continue;
        }

        char **argv = p.we_wordv;
        int argc = p.we_wordc;

        if (!strcmp(argv[0], "add")) {
            if (argc >= 3) {
                for (int i = 2; i < argc; i++)
                    cmd_add(argv[1], argv[i]);
            } else {
                printf("Usage: add <src> <target...>\n");
            }
        }
        else if (!strcmp(argv[0], "end")) {
            if (argc >= 3) {
                for (int i = 2; i < argc; i++)
                    cmd_end(argv[1], argv[i]);
            } else {
                printf("Usage: end <src> <target...>\n");
            }
        }
        else if (!strcmp(argv[0], "restore")) {
            if (argc == 3)
                cmd_restore(argv[1], argv[2]);
            else
                printf("Usage: restore <src> <target>\n");
        }
        else if (!strcmp(argv[0], "list")) {
            cmd_list();
        }
        else if (!strcmp(argv[0], "exit")) {
            wordfree(&p);
            break;
        }
        else {
            printf("Unknown command\n");
        }

        wordfree(&p);
    }

    cleanup_backups();

    return 0;
}
