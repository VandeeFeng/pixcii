#define _POSIX_C_SOURCE 200809L
#include "image.h"
#include "ascii_art.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// check if a string is a URL
static bool isURL(const char *input)
{
    return strncmp(input, "http://", 7) == 0 || strncmp(input, "https://", 8) == 0;
}

// Quote one argument for the shell used by curl and wget.
static char *quoteArgument(const char *argument)
{
    size_t length = strlen(argument);
    if (length > (SIZE_MAX - 3) / 4) return NULL;
    char *quoted = malloc(length * 4 + 3);
    if (!quoted) return NULL;
    char *cursor = quoted;
    *cursor++ = '\'';
    for (const char *source = argument; *source; source++) {
        if (*source == '\'') {
            memcpy(cursor, "'\\''", 4);
            cursor += 4;
        } else {
            *cursor++ = *source;
        }
    }
    *cursor++ = '\'';
    *cursor = '\0';
    return quoted;
}

// Function to get file extension from URL or filename
static void getFileExtension(const char *path, char extension[6])
{
    const char *end = strchr(path, '?');
    if (!end) end = path + strlen(path);
    const char *dot = NULL;
    for (const char *cursor = path; cursor < end; cursor++) {
        if (*cursor == '/') dot = NULL;
        if (*cursor == '.') dot = cursor;
    }
    // Validate extension doesn't contain path separators
    if (dot && end - dot <= 5) {
        memcpy(extension, dot, (size_t)(end - dot));
        extension[end - dot] = '\0';
    }
}

// Function to get content-type from URL using HTTP HEAD request
static void getContentTypeFromURL(const char *quoted_url, char extension[6])
{
    size_t capacity = strlen(quoted_url) + 96;
    char *command = malloc(capacity);
    if (!command) return;
    snprintf(command, capacity, "curl -s -I %s", quoted_url);
    FILE *pipe = popen(command, "r");
    free(command);
    if (!pipe) return;
    static const struct {
        const char *content_type;
        const char *extension;
    } types[] = {
        {"image/jpeg", ".jpg"}, {"image/png", ".png"},
        {"image/gif", ".gif"}, {"image/webp", ".webp"},
        {"image/bmp", ".bmp"}, {"video/mp4", ".mp4"},
        {"video/quicktime", ".mov"}, {"video/x-msvideo", ".avi"},
        {"video/x-matroska", ".mkv"}, {"video/webm", ".webm"}
    };
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        // Convert result to lowercase for case-insensitive comparison
        for (char *cursor = line; *cursor; cursor++) *cursor = (char)tolower((unsigned char)*cursor);
        if (strncmp(line, "content-type:", sizeof("content-type:") - 1) != 0) continue;
        // Map content-types to extensions
        for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); index++) {
            if (strstr(line, types[index].content_type)) {
                strcpy(extension, types[index].extension);
                break;
            }
        }
    }
    pclose(pipe);
    // Unknown or unsupported type
}

// Function to download file from URL
static char *downloadFromURL(const char *url)
{
    // Generate temporary filename
    char extension[6] = "";
    char *quoted_url = quoteArgument(url);
    char *path = NULL;
    char *quoted_path = NULL;
    char *command = NULL;
    if (!quoted_url) goto failure;
    // First try to get extension from URL path
    getFileExtension(url, extension);
    // If no valid extension found in URL, use HTTP HEAD request
    if (!extension[0]) getContentTypeFromURL(quoted_url, extension);
    // Final fallback
    if (!extension[0]) strcpy(extension, ".jpg"); // Default to JPG for unknown image types
    // Create proper temp file path
    const char *directory = getenv("TMPDIR");
    if (!directory || !directory[0]) directory = "/tmp";
    size_t path_capacity = strlen(directory) + sizeof("/pixcii_temp") + sizeof(extension);
    path = malloc(path_capacity);
    if (!path) goto failure;
    snprintf(path, path_capacity, "%s/pixcii_temp%s", directory, extension);
    quoted_path = quoteArgument(path);
    if (!quoted_path) goto failure;
    size_t command_capacity = strlen(quoted_url) + strlen(quoted_path) + 128;
    command = malloc(command_capacity);
    if (!command) goto failure;
    // Try curl first, then wget as fallback
    snprintf(command, command_capacity,
             "curl -L -s -o %s %s", quoted_path, quoted_url);
    int result = system(command);
    if (result != 0) {
        snprintf(command, command_capacity,
                 "wget -q -O %s %s", quoted_path, quoted_url);
        if (system(command) != 0) {
            fprintf(stderr, "Failed to download file. Please ensure curl or wget is installed.\n");
            goto failure;
        }
    }
    // Check if file was actually downloaded and has content
    struct stat information;
    if (stat(path, &information) != 0 || information.st_size == 0) {
        fprintf(stderr, "Downloaded file is empty or failed to download.\n");
        goto failure;
    }
    free(command);
    free(quoted_path);
    free(quoted_url);
    return path;
failure:
    if (path) remove(path);
    free(path);
    free(command);
    free(quoted_path);
    free(quoted_url);
    fprintf(stderr, "Error downloading from URL: %s\n", url);
    return NULL;
}

// Function to display the command-line usage help message
// program_name: The name of the executable (argv[0])
static void displayHelp(const char *program_name)
{
    printf("Usage: %s -i <input> [options]\n\n", program_name);
    puts("Required:\n"
         "  -i, --input <path|url>      Path to input media file or URL\n\n"
         "Options:\n"
         "  -o, --output <path>         Path to save output ASCII art\n"
         "  -c, --color                 Enable colored ASCII output using ANSI escape codes\n"
         "      --block <shape>        Select ▀, ▄, or █ and enable block rendering\n"
         "  -g, --original              Display media at original resolution\n"
         "  -s, --scale <float>         Size multiplier: 0.5 halves, 2 doubles (requires --original)\n"
         "  -a, --aspect-ratio <float>  Set character aspect ratio (default: 2.0)\n"
         "  -b, --brightness <float>    Adjust brightness multiplier (default: 1.0)\n"
         "  -n, --invert                Invert brightness mapping\n"
         "  -e, --edges                 Detect edges instead of brightness for character selection\n"
         "  -m, --chars <string>        ASCII character set (default: \" .:-=+*#%@\")\n"
         "  -d, --delay <ms>            Frame delay in milliseconds for videos (default: auto)\n"
         "  -h, --help                  Show this help message\n\n"
         "Examples:");
    printf("  %s -i image.jpg -c\n", program_name);
    printf("  %s -i video.mp4\n", program_name);
    printf("  %s -i https://example.com/image.jpg -c\n", program_name);
    printf("  %s -i large_image.png -g -s 0.5\n", program_name);
}

// Parse a numeric option and report invalid input or values outside the float range.
static int parseFloat(const char *argument, const char *option, float *value)
{
    char *end;
    errno = 0;
    float parsed = strtof(argument, &end);
    // Handle invalid float input and values outside the float range, as with std::stof.
    if (end == argument) {
        fprintf(stderr, "Error: Invalid argument for option '%s'. Expected a number.\n", option);
        return -1;
    }
    if (errno == ERANGE) {
        fprintf(stderr, "Error: Argument for option '%s' out of float range.\n", option);
        return -1;
    }
    *value = parsed;
    return 0;
}

// Main function - entry point of the program
// Handles command-line argument parsing and calls the appropriate processing function
int main(int argc, char *argv[])
{
    // Create a struct to hold parsed parameters
    AsciiArtParams params = {
        .ascii_chars = " .:-=+*#%@",
        .brightness_boost = 1.0f,
        .scale = 1.0f,
        .aspect_ratio = 2.0f,
        .auto_fit = true
    };
    int frame_delay = DEFAULT_FRAME_DELAY;
    char *temporary_file = NULL;
    int result = EXIT_FAILURE;
    // --- Command Line Parsing ---
    for (int index = 1; index < argc; index++) { // Start from 1 to skip the program name (argv[0])
        const char *argument = argv[index]; // Get the current argument as a string
        // Boolean flags
        if (!strcmp(argument, "-g") || !strcmp(argument, "--original")) {
            params.auto_fit = false; // Disable auto-fit for original sizing
        } else if (!strcmp(argument, "-c") || !strcmp(argument, "--color")) {
            params.color = true; // Set the color flag
        } else if (!strcmp(argument, "-n") || !strcmp(argument, "--invert")) {
            params.invert_color = true; // Set the invert flag
        } else if (!strcmp(argument, "-e") || !strcmp(argument, "--edges")) {
            params.detect_edges = true; // Set the detect_edges flag
        } else if (!strcmp(argument, "-h") || !strcmp(argument, "--help")) {
            displayHelp(argv[0]); // Display help and exit
            result = EXIT_SUCCESS;
            goto cleanup;
        } else {
            // Check for known options
            enum { INPUT, OUTPUT, CHARACTERS, BRIGHTNESS, SCALE, ASPECT, DELAY, BLOCK } option;
            if (!strcmp(argument, "-i") || !strcmp(argument, "--input")) option = INPUT;
            else if (!strcmp(argument, "-o") || !strcmp(argument, "--output")) option = OUTPUT;
            else if (!strcmp(argument, "-m") || !strcmp(argument, "--chars")) option = CHARACTERS;
            else if (!strcmp(argument, "-b") || !strcmp(argument, "--brightness")) option = BRIGHTNESS;
            else if (!strcmp(argument, "-s") || !strcmp(argument, "--scale")) option = SCALE;
            else if (!strcmp(argument, "-a") || !strcmp(argument, "--aspect-ratio")) option = ASPECT;
            else if (!strcmp(argument, "-d") || !strcmp(argument, "--delay")) option = DELAY;
            else if (!strcmp(argument, "--block")) option = BLOCK;
            else {
                // Handle unknown arguments
                fprintf(stderr, "Error: Unknown argument '%s'.\n", argument);
                displayHelp(argv[0]);
                goto cleanup;
            }
            // Error if option requires an argument but none is provided
            if (index + 1 >= argc) {
                fprintf(stderr, "Error: Option '%s' requires an argument.\n", argument);
                displayHelp(argv[0]);
                goto cleanup;
            }
            const char *value = argv[++index];
            switch (option) {
                case INPUT:
                    // Check if input is a URL
                    if (temporary_file) {
                        remove(temporary_file);
                        free(temporary_file);
                        temporary_file = NULL;
                    }
                    if (isURL(value)) {
                        temporary_file = downloadFromURL(value);
                        if (!temporary_file) goto cleanup;
                        params.input_path = temporary_file;
                    } else {
                        params.input_path = value;
                    }
                    break;
                case OUTPUT: params.output_path = value; break; // Assign and advance
                case CHARACTERS: params.ascii_chars = value; break; // Assign and advance
                case BLOCK:
                    if (!strcmp(value, "▀")) params.block_shape = BLOCK_UPPER;
                    else if (!strcmp(value, "▄")) params.block_shape = BLOCK_LOWER;
                    else if (!strcmp(value, "█")) params.block_shape = BLOCK_FULL;
                    else {
                        fprintf(stderr, "Error: --block expects ▀, ▄, or █.\n");
                        goto cleanup;
                    }
                    params.render_mode = RENDER_BLOCK;
                    break;
                case BRIGHTNESS:
                    // Convert next arg to float and assign
                    if (parseFloat(value, argument, &params.brightness_boost) != 0) {
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    break;
                case SCALE:
                    // Convert next arg to float and assign
                    if (parseFloat(value, argument, &params.scale) != 0) {
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    break;
                case ASPECT:
                    // Convert next arg to float and assign
                    if (parseFloat(value, argument, &params.aspect_ratio) != 0) {
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    break;
                case DELAY: {
                    char *end;
                    errno = 0;
                    // Convert next arg to int and assign, accepting a numeric prefix as std::stoi does.
                    long parsed = strtol(value, &end, 10);
                    if (end == value) {
                        fprintf(stderr, "Error: Invalid argument for option '%s'. Expected an integer.\n", argument);
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    if (errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
                        fprintf(stderr, "Error: Argument for option '%s' out of integer range.\n", argument);
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    frame_delay = (int)parsed;
                    if (frame_delay < 0) {
                        fprintf(stderr, "Error: Frame delay must be non-negative.\n");
                        displayHelp(argv[0]);
                        goto cleanup;
                    }
                    break;
                }
            }
        }
    }
    // --- End Command Line Parsing ---

    // --- Parameter Validation ---
    // Check if the required input path was provided
    if (!params.input_path || !params.input_path[0]) {
        fprintf(stderr, "Error: Input file path or URL is required (--input or -i option).\nUse -h or --help for usage information.\n");
        goto cleanup;
    }
    // Check if the character set is empty
    if (params.render_mode == RENDER_ASCII && !params.ascii_chars[0]) {
        fprintf(stderr, "Error: ASCII character set cannot be empty (--chars or -m option).\n");
        goto cleanup;
    }
    // Check if scale or aspect ratio are non-positive
    if (params.scale <= 0) {
        fprintf(stderr, "Error: Scale factor (--scale or -s) must be positive.\n");
        goto cleanup;
    }
    if (params.aspect_ratio <= 0) {
        fprintf(stderr, "Error: Aspect ratio (--aspect-ratio or -a) must be positive.\n");
        goto cleanup;
    }
    // --- End Parameter Validation ---

    // --- File Type Detection and Processing ---
    // Determine if input is video/GIF or static image and process accordingly
    int processing_result;
    if (isVideoFile(params.input_path)) {
        // Process video/GIF
        processing_result = processVideo(params.input_path, &params, frame_delay);
    } else {
        // Process static image using existing function
        processing_result = processImage(&params);
    }
    // --- End File Type Detection and Processing ---
    if (processing_result == 0) result = EXIT_SUCCESS;
cleanup:
    // Clean up temporary file if it was downloaded
    if (temporary_file) remove(temporary_file);
    free(temporary_file);
    return result;
}
