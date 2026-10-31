#include "edge_detection.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// --- Constants for Sobel Operator ---
// Standard 3x3 Sobel kernel for detecting horizontal gradients
static const int sobel_x[3][3] = {
    {-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}
};
// Standard 3x3 Sobel kernel for detecting vertical gradients
static const int sobel_y[3][3] = {
    {-1, -2, -1}, {0, 0, 0}, {1, 2, 1}
};

// --- End Constants ---

// Perform Sobel edge detection on an image
// image: The input Image struct
// Returns: A caller-owned array of floats representing the magnitude of the gradient at each pixel
float *detectEdges(const Image *image)
{
    // Convert the image to grayscale, as Sobel operates on single-channel images
    uint8_t *gray = rgbToGrayscale(image);
    if (!gray) return NULL;
    size_t count = (size_t)image->width * image->height;
    // Array to store the calculated edge magnitude for each pixel
    // Initialized with size image.width * image.height and values 0.0f
    float *magnitudes = calloc(count, sizeof(*magnitudes));
    if (!magnitudes) {
        fprintf(stderr, "Failed to allocate edge magnitudes.\n");
        free(gray);
        return NULL;
    }
    float maximum = 0.0f;
    // Iterate through the image pixels, excluding the border (1 pixel wide)
    // The Sobel operator is a 3x3 kernel, so it cannot be applied to the outermost pixels
    for (int y = 1; y < image->height - 1; y++) {
        for (int x = 1; x < image->width - 1; x++) {
            int gradient_x = 0; // Horizontal gradient sum
            int gradient_y = 0; // Vertical gradient sum
            // Apply the Sobel operator kernels (sobel_x and sobel_y) to the 3x3 neighborhood
            // around the current pixel (x, y)
            for (int kernel_y = -1; kernel_y <= 1; kernel_y++) { // Iterate over kernel rows relative to pixel
                for (int kernel_x = -1; kernel_x <= 1; kernel_x++) { // Iterate over kernel columns relative to pixel
                    // Get the grayscale value of the neighboring pixel
                    int pixel = gray[(size_t)(y + kernel_y) * image->width + x + kernel_x];

                    // Accumulate weighted sums for horizontal and vertical gradients
                    gradient_x += pixel * sobel_x[kernel_y + 1][kernel_x + 1];
                    gradient_y += pixel * sobel_y[kernel_y + 1][kernel_x + 1];
                }
            }
            // Calculate the magnitude of the gradient using the Euclidean distance (sqrt(gx^2 + gy^2))
            float magnitude = sqrtf((float)(gradient_x * gradient_x + gradient_y * gradient_y));
            magnitudes[(size_t)y * image->width + x] = magnitude;
            // Find the maximum edge magnitude in the result array
            if (magnitude > maximum) maximum = magnitude;
        }
    }
    // --- Normalization ---
    // Normalize the edge magnitudes to the range [0, 255]
    // This scales the magnitudes so they can be used similarly to brightness values (0-255)
    if (maximum > 0.0f) { // Avoid division by zero if no edges were detected
        for (size_t index = 0; index < count; index++) {
            magnitudes[index] = magnitudes[index] / maximum * 255.0f;
        }
    }
    free(gray);
    // Return the array of normalized edge magnitudes
    return magnitudes;
}
