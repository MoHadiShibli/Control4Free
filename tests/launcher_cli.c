#include "autorun.h"
#include "service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    char message[160] = {0};
    if (argc < 2) return 2;
    if (getenv("C4F_TEST_HOST")) c4fLauncherSetHost(getenv("C4F_TEST_HOST"));
    if (!strncmp(argv[1], "autorun-", 8)) {
        /* autorun-check|autorun-on|autorun-off <root> [bundled.elf] */
        if (argc < 3) return 2;
        c4fAutorunSetRoot(argv[2]);
        if (argc > 3 && c4fAutorunLoadBundled(argv[3])) { printf("unreadable\n"); return 0; }
        if (!strcmp(argv[1], "autorun-check")) {
            char note[96];
            int state = c4fAutorunCheck(note, sizeof(note));
            printf("%d|%s\n", state, note);
            return 0;
        }
        int result = !strcmp(argv[1], "autorun-on") ? c4fAutorunEnable(message, sizeof(message)) :
                     !strcmp(argv[1], "autorun-off") ? c4fAutorunDisable(message, sizeof(message)) : 2;
        printf("%d %s\n", result, message);
        return result == 2 ? 2 : 0;
    }
    if (!strcmp(argv[1], "probe")) {
        C4fServiceStatus s;
        int result = c4fLauncherProbe(&s);
        printf("%d %d %d %s|%s\n", result, s.controllers, s.stopping, s.version, c4fLauncherProblem());
        return 0;
    }
    if (!strcmp(argv[1], "start") && argc == 3) {
        size_t size = 0;
        const unsigned char *payload = c4fAutorunLoadBundled(argv[2]) ? NULL : c4fAutorunBundled(&size);
        int result = c4fLauncherStart(payload, size, message, sizeof(message));
        printf("%d %s\n", result, message); return 0;
    }
    int result = c4fLauncherStop(message, sizeof(message));
    printf("%d %s\n", result, message); return 0;
}
