#ifndef PIXCII_IMAGE_H
#define PIXCII_IMAGE_H

#include <stddef.h>
#include <stdint.h>

// Structure to hold image data
typedef struct {
    uint8_t *data; // Pixel data (e.g., RGBRGB...)
    int width; // Image width in pixels
    int height; // Image height in pixels
    int channels; // Number of color channels per pixel (e.g., 3 for RGB, 4 for RGBA)

    // Initialize members with Image image = {0} in C.
    // Pixel data must be released explicitly with freeImage.
} Image;

// Structure to hold terminal dimensions
typedef struct {
    int width; // Terminal width in characters.
    int height; // Terminal height in characters.
} TerminalSize;

// --- Constants ---
// Standard luminance weights for RGB to Grayscale conversion (Rec. 601 standard)
// These weights are used when converting a color image to a single grayscale value per pixel.
#define GRAYSCALE_WEIGHT_R 0.299f
#define GRAYSCALE_WEIGHT_G 0.587f
#define GRAYSCALE_WEIGHT_B 0.114f
// --- End Constants ---

// --- Function Declarations ---

// Load an image from a file path using stb_image.
// path: The path to the image file.
// image: Receives the loaded image data and dimensions.
// Returns: 0 on success, or -1 if the image fails to load.
int loadImage(const char *path, Image *image);
void freeImage(Image *image);

// Resize an image using a scale factor and adjust height based on character aspect ratio.
// image: The input Image struct.
// scale: A scaling factor (e.g., 1.0 for no scaling, 2.0 for half the input width).
// aspect_ratio: The aspect ratio (width/height) of characters used for output.
// resized: Receives a new Image struct with the resized image data.
// Returns: 0 on success, or -1 on failure.
// Note: Uses stb_image_resize2 interpolation (see image.c for details).
int resizeImage(const Image *image, float scale, float aspect_ratio, Image *resized);
int resizeImageToSize(const Image *image, int width, int height, Image *resized);

// Resize an image to fit the current terminal dimensions.
// image: The input Image struct.
// aspect_ratio: The aspect ratio of characters (width/height).
// The caller only invokes this when auto-fitting is enabled.
// resized: Receives a new Image struct resized to fit the terminal.
int resizeImageToTerminal(const Image *image, float aspect_ratio, Image *resized);

// Convert an RGB image to grayscale.
// image: The input Image struct (expected to have at least 3 channels).
// Returns: A caller-owned array of uint8_t containing the grayscale values (0-255) for each pixel.
// Returns NULL on allocation failure, or zeros if the image has insufficient channels.
uint8_t *rgbToGrayscale(const Image *image);

// Get the current size of the terminal window.
// Returns: A TerminalSize struct with the width and height in characters.
// Provides a default size if terminal size cannot be determined.
TerminalSize getTerminalSize(void);

#endif
