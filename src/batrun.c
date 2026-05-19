#include "cmd_event.h"
#include "cmd_report.h"
#include "cmd_status.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

static void print_header(FILE *out) {
    fprintf(out,
        "batrun " BATRUN_VERSION " - " BATRUN_TAGLINE "\n"
        BATRUN_COPYRIGHT "\n"
        BATRUN_LICENSE "\n");
}

static int usage(FILE *out, int code) {
    print_header(out);
    fputs(
        "\n"
        "Usage:\n"
        "  batrun status              show current battery + estimate\n"
        "  batrun report [options]    aggregate report over a time window\n"
        "                             --last <N{h,d,w,m,y}> | --since YYYY-MM-DD\n"
        "                             --month YYYY-MM | --year YYYY | --all\n"
        "  batrun event <type>        record a power event (root)\n"
        "                             types: boot shutdown sleep resume ac_on ac_off\n"
        "  batrun version             print version\n"
        "  batrun help                show this message\n",
        out);
    return code;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage(stderr, 2);
    const char *cmd = argv[1];

    if (strcmp(cmd, "event")  == 0) return cmd_event (argc - 1, argv + 1);
    if (strcmp(cmd, "report") == 0) return cmd_report(argc - 1, argv + 1);
    if (strcmp(cmd, "status") == 0) return cmd_status(argc - 1, argv + 1);

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        print_header(stdout);
        return 0;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0)
        return usage(stdout, 0);

    fprintf(stderr, "batrun: unknown command: %s\n", cmd);
    return usage(stderr, 2);
}
