#ifndef COMMANDS_H
#define COMMANDS_H

#include <wordexp.h>

int parse_command(const char *line, wordexp_t *p);
void cmd_add(char *src, char *dst);
void cmd_list(void);
void cmd_end(char *src, char *dst);
void cmd_restore(const char *source, const char *target);
void cleanup_backups(void);

#endif
