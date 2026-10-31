#ifndef PIXCII_ASCII_ART_H
#define PIXCII_ASCII_ART_H

#include "image.h"
#include <stdbool.h>

typedef enum {
    RENDER_ASCII,
    RENDER_BLOCK
} RenderMode;

typedef enum {
    BLOCK_UPPER,
    BLOCK_LOWER,
    BLOCK_FULL
} BlockShape;

// Structure to hold parameters for ASCII art generation
typedef struct {
    const char *input_path;
    const char *output_path;
    const char *ascii_chars; // Default character set: " .:-=+*#%@"
    bool color; // Enable color output using ANSI escape codes.
    RenderMode render_mode;
    BlockShape block_shape;
    bool invert_color; // Invert brightness mapping.
    float brightness_boost; // Multiplier for brightness.
    float scale; // Scale factor for resizing the image.
    bool detect_edges; // Use edge magnitude instead of brightness.
    float aspect_ratio; // Character aspect ratio (width / height).
    bool auto_fit; // Automatically resize to fit terminal
} AsciiArtParams;

enum { DEFAULT_FRAME_DELAY = 100 };

// --- Function Declarations ---

// Process an image based on provided parameters and generate ASCII art output
// params: Configuration parameters for the ASCII art generation
// Returns: 0 on success (including rejected scaled dimensions), or a negative value on failure.
int processImage(const AsciiArtParams *params);

// Generate ASCII art as a string based on the processed image and parameters
// image: The Image struct containing pixel data
// params: Configuration parameters
// edge_magnitudes: Optional pointer to pre-calculated edge magnitudes (used if detect_edges is true)
// Returns: A caller-owned string containing the generated ASCII art, or NULL on failure.
char *generateAsciiText(const Image *image, const AsciiArtParams *params,
                       const float *edge_magnitudes);

// Process video/GIF frame by frame using existing ASCII art pipeline
// path: Path to video/GIF
// params: ASCII art parameters (same as used for static images)
// frame_delay: Delay between frames in milliseconds for animation effect
// DEFAULT_FRAME_DELAY selects automatic frame timing.
// Returns: 0 on success, or a negative value on failure.
int processVideo(const char *path, const AsciiArtParams *params, int frame_delay);

// Check if file is a supported video/GIF format
// filename: Input file path to check
// Returns: true if file extension matches supported video/GIF formats, false otherwise
bool isVideoFile(const char *filename);

#endif
