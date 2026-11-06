#include <stdio.h>
#include <string.h>

/* Return zero when there is no more input. An oversized line is discarded
   in full so its remaining text cannot be mistaken for another command. */
static int read_command(char *line, int capacity)
{
    int length = 0;
    int too_long = 0;
    int character;

    while ((character = getchar()) != EOF && character != '\n') {
        if (length < capacity - 1) {
            line[length] = (char)character;
            length++;
        } else {
            too_long = 1;
        }
    }

    line[length] = '\0';
    if (too_long)
        line[0] = '\0';

    return character != EOF || length > 0;
}

int main(void)
{
    char line[4096];
    char *command;

    while (read_command(line, sizeof(line))) {
        command = strtok(line, " \t\r");
        if (command == NULL)
            continue;

        if (strcmp(command, "uci") == 0) {
            puts("id name Learning Chess");
            puts("id author Learning Chess contributors");
            puts("uciok");
        } else if (strcmp(command, "isready") == 0) {
            puts("readyok");
        } else if (strcmp(command, "quit") == 0) {
            break;
        }

        /* A GUI may wait for our reply before sending its next command. */
        fflush(stdout);
    }

    return 0;
}
