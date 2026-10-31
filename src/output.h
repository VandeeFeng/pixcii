#ifndef PIXCII_OUTPUT_H
#define PIXCII_OUTPUT_H

// --- Function Declarations ---

// Saves a string of text to a specified file path.
// ascii_text: The string content to save.
// output_path: The path to the output file.
// Returns: 0 on success, or -1 if the file cannot be opened or written.
int saveOutputText(const char *ascii_text, const char *output_path);

#endif
