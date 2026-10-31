#include "output.h"
#include <stdio.h>

// Save the generated ASCII text to a file.
// ascii_text: The string containing the ASCII art.
// output_path: The path to the file where the text should be saved.
int saveOutputText(const char *ascii_text, const char *output_path)
{
    // Create an output stream to write to the file
    FILE *file = fopen(output_path, "w");

    // Check if the file was successfully opened
    if (!file) {
        // If not, return an error indicating the file could not be opened
        fprintf(stderr, "Failed to open output file for writing: %s\n", output_path);
        return -1;
    }
    // Write the entire ASCII text string to the file
    int result = fputs(ascii_text, file);

    // Close the file stream. This is important to ensure data is written and resources are released.
    // C streams must be closed explicitly when no longer needed.
    int close_result = fclose(file);
    if (result == EOF || close_result == EOF) {
        fprintf(stderr, "Failed to write output file: %s\n", output_path);
        return -1;
    }
    return 0;
}
