#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE2_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include "image.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// For terminal size detection - platform specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// Load an image from a file path using the stb_image library.
// Handles common image formats (like JPG, PNG, TGA, BMP, GIF, PSD, PIC).
int loadImage(const char *path, Image *image)
{
    // Load the image data. Pass 0 for desired_channels to keep original channels.
    // stb_image loads data into a byte array (uint8_t*).
    // Populate our Image struct with the loaded data and dimensions.
    image->data = stbi_load(path, &image->width, &image->height, &image->channels, 0);
    // Check if image loading failed
    if (!image->data) {
        // Return an error with a descriptive message if loading fails
        fprintf(stderr, "Failed to load image: %s - %s\n", path, stbi_failure_reason());
        return -1;
    }
    // The loaded pixel buffer is owned by our Image struct without an extra copy.
    // Return success with the populated Image struct.
    return 0;
}

// Free the data allocated by stb_image_load or the image resizing functions.
// Unlike the original vector, image data must be explicitly freed in C.
void freeImage(Image *image)
{
    free(image->data);
    *image = (Image){0};
}

// Allocate a new image with the requested dimensions for stb_image_resize2 interpolation.
int resizeImageToSize(const Image *image, int width, int height, Image *resized)
{
    if (width <= 0 || height <= 0 || image->channels <= 0 ||
        width > INT_MAX / image->channels ||
        (size_t)width > SIZE_MAX / (size_t)height / (size_t)image->channels) {
        fprintf(stderr, "Invalid resized image dimensions.\n");
        return -1;
    }
    // Allocate memory for the resized image data buffer. Use size_t for size calculation.
    Image output = {
        .width = width,
        .height = height,
        .channels = image->channels,
        .data = malloc((size_t)width * height * image->channels)
    };
    if (!output.data) {
        fprintf(stderr, "Failed to allocate resized image.\n");
        return -1;
    }
    // --- Use stb_image_resize2 for Interpolation ---

    // Call the appropriate resize function from stb_image_resize2.h (stbir_resize_uint8_linear)
    // We need to map image.channels (int) to the stbir_pixel_layout enum.
    // Common values are STBIR_1CHANNEL (1), STBIR_RGB (3), STBIR_RGBA (4).
    // Cast the channel count to the enum type; its values match the channel counts.
    // Check for success: the linear function returns the output pointer, or NULL on failure.
    if (!stbir_resize_uint8_linear(image->data, image->width, image->height,
                                  image->width * image->channels, // input_stride_bytes
                                  output.data, width, height, // output_pixels, output_w, output_h
                                  width * image->channels, // output_stride_bytes
                                  (stbir_pixel_layout)image->channels)) { // pixel_layout
        // Handle the error and release the newly allocated image buffer.
        fprintf(stderr, "Image resizing failed using stb_image_resize2 (linear). Returned NULL.\n");
        freeImage(&output);
        return -1;
    }
    // --- End stb_image_resize2 ---
    // Return the resized image struct through the output parameter.
    *resized = output;
    return 0;
}

// Resize an image using a scale factor and adjust height based on character aspect ratio.
// This implementation uses stb_image_resize2 for high-quality interpolation.
// image: The input Image struct.
// scale: A scaling factor (e.g., 1.0 for no scaling, 2.0 for half the input width).
// aspect_ratio: The aspect ratio (width / height) of characters used for output.
// Returns: A new Image struct with the resized image data through resized.
int resizeImage(const Image *image, float scale, float aspect_ratio, Image *resized)
{
    // Calculate new dimensions based on the scale factor and aspect ratio.
    // Note: scale > 1.0 typically means the resulting ASCII art has fewer characters.
    // Integer dimensions can introduce slight inaccuracies.
    float width = (float)image->width / scale;
    float height = (float)image->height / scale / aspect_ratio;
    if (!isfinite(width) || !isfinite(height) ||
        (double)width > INT_MAX || (double)height > INT_MAX) {
        fprintf(stderr, "Invalid resized image dimensions.\n");
        return -1;
    }
    // Ensure dimensions are at least 1x1 pixel
    return resizeImageToSize(image, (int)fmaxf(width, 1.0f),
                             (int)fmaxf(height, 1.0f), resized);
}

// Convert an RGB image to grayscale using standard luminance weights.
// image: The input Image struct. Assumes at least 3 channels (RGB).
// Returns: A caller-owned array containing the grayscale value (0-255) for each pixel.
// Returns NULL on allocation failure, or zeros if the image has insufficient channels.
uint8_t *rgbToGrayscale(const Image *image)
{
    size_t count = (size_t)image->width * image->height;
    uint8_t *gray = calloc(count, sizeof(*gray));
    if (!gray) {
        fprintf(stderr, "Failed to allocate grayscale image.\n");
        return NULL;
    }
    // Check if the image has enough channels for RGB conversion
    if (image->channels < 3) {
        // If not, print a warning and return a default grayscale representation (all zeros)
        fprintf(stderr, "Warning: Image does not have enough channels (%d) for RGB to Grayscale conversion. Expected 3 or more.\n", image->channels);
        return gray;
    }
    // Iterate through each pixel of the image
    for (size_t index = 0; index < count; index++) {
        // Calculate the starting index of the pixel's RGB data
        const uint8_t *pixel = image->data + index * image->channels;
        // Get the R, G, and B components and convert using standard Rec. 601 weights
        gray[index] = (uint8_t)(GRAYSCALE_WEIGHT_R * pixel[0] +
                                GRAYSCALE_WEIGHT_G * pixel[1] +
                                GRAYSCALE_WEIGHT_B * pixel[2]);
    }
    // Return the grayscale image data
    return gray;
}

// Get the current size of the terminal window using platform-specific APIs.
// Returns a default size (80x24) if determination fails.
TerminalSize getTerminalSize(void)
{
    TerminalSize size = {80, 24}; // Default fallback size (standard terminal dimensions)
#ifdef _WIN32
    // Windows implementation using GetConsoleScreenBufferInfo
    CONSOLE_SCREEN_BUFFER_INFO info;
    // Get handle to the standard output console
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(console, &info)) {
        // Calculate width and height from the console window rectangle
        size.width = info.srWindow.Right - info.srWindow.Left + 1;
        size.height = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    // Unix-like systems implementation using ioctl and TIOCGWINSZ
    // Subtract a small margin (e.g., 5) to avoid issues with line wrapping/prompts
    const int terminal_margin = 5;
    struct winsize window;
    // Use ioctl to get window size for standard output file descriptor
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) != -1) {
        // Get columns (width) and rows (height) from winsize struct
        size.width = window.ws_col > terminal_margin ? window.ws_col - terminal_margin : window.ws_col;
        size.height = window.ws_row > terminal_margin ? window.ws_row - terminal_margin : window.ws_row;
        // Ensure minimum size
        if (size.width < 1) size.width = 1;
        if (size.height < 1) size.height = 1;
    }
#endif
    // Return the determined (or default) terminal size
    return size;
}

// Resize image to fit the current terminal dimensions, adjusting for character aspect ratio.
// This calculates the scale factor needed to fit the image within the terminal.
// image: The input Image struct.
// aspect_ratio: The aspect ratio of characters (width/height).
// Called only when auto-fitting is enabled; otherwise the caller keeps the original image.
// Returns: The resized Image struct through resized.
int resizeImageToTerminal(const Image *image, float aspect_ratio, Image *resized)
{
    // Get the current terminal size
    TerminalSize terminal = getTerminalSize();
    // Calculate available terminal height, leaving a small margin for prompts/status
    int height = terminal.height > 1 ? terminal.height - 1 : 1;
    // Calculate the required scale factors to fit within terminal width and height.
    // Scale based on width: original width / terminal width in characters
    float scale_width = (float)image->width / terminal.width;
    // Scale based on height: original height / (terminal height * character aspect ratio)
    float scale_height = (float)image->height / ((float)height * aspect_ratio);
    // Use the LARGER scale factor so the image fits entirely within terminal dimensions.
    float scale = fmaxf(scale_width, scale_height);
    // Ensure the scale is a reasonable positive value.
    // Avoid division by zero or near-zero, and prevent extreme scaling.
    const float minimum_scale = 0.0001f;
    const float maximum_scale = 10000.0f;
    scale = fminf(fmaxf(scale, minimum_scale), maximum_scale);
    // Use the generic resizeImage function with the calculated scale and aspect ratio.
    // resizeImage will re-calculate the new dimensions based on this scale value.
    return resizeImage(image, scale, aspect_ratio, resized);
}
