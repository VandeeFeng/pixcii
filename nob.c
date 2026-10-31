#define NOB_IMPLEMENTATION
#define NOB_UNSTRIP_PREFIX
#include "nob.h"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "nob.h");
    if (argc != 1) {
        nob_log(NOB_ERROR, "Usage: %s", argv[0]);
        return 1;
    }

    const char *compiler = getenv("CC");
    if (!compiler || !*compiler) compiler = "gcc";
    const char *include_directory = getenv("FFMPEG_INCLUDE_DIR");
    const char *library_directory = getenv("FFMPEG_LIB_DIR");
    Nob_Cmd command = {0};

    if (!nob_mkdir_if_not_exists("build")) return 1;
    nob_cmd_append(&command, compiler, "-std=c17", "-Wall", "-Wextra", "-O2",
                   "-Wno-missing-field-initializers", "-o", "build/pixcii");
    if (include_directory && *include_directory) {
        nob_cmd_append(&command, "-I", include_directory);
    }
    if (library_directory && *library_directory) {
        nob_cmd_append(&command, "-L", library_directory);
    }
    nob_cmd_append(&command, "src/main.c", "src/image.c", "src/ascii_art.c",
                   "src/edge_detection.c", "src/output.c",
                   "-lavformat", "-lavcodec", "-lswscale", "-lavutil", "-lm");
    bool success = nob_cmd_run(&command);
    nob_cmd_free(command);
    return success ? 0 : 1;
}
