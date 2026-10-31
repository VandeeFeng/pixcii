#include "ascii_art.h"
#include "edge_detection.h"
#include "output.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

// Information about a single pixel for character selection
typedef struct {
    uint64_t brightness; // Grayscale brightness value of the pixel
    float edge_magnitude; // Edge magnitude if edge detection is enabled
    uint8_t color[3]; // RGB color values of the pixel
} PixelInfo;

// Calculate relevant information (brightness, color, edge_magnitude) for a single pixel
// This function processes one pixel at the given (x, y) coordinates
static PixelInfo getPixelInfo(const Image *image, int x, int y,
                             const AsciiArtParams *params, const float *edges)
{
    PixelInfo info = {0}; // Create a struct to hold pixel information

    // Initialize color array with zeros
    // --- Bounds Check ---
    // Basic bounds check to ensure coordinates are within image dimensions
    // Although calling code should ideally provide valid coordinates, this adds safety
    if (x < 0 || x >= image->width || y < 0 || y >= image->height) {
        // If out of bounds, return default (zero-initialized) info
        return info;
    }

    // Calculate the starting index of the pixel's data in the image buffer
    size_t index = (size_t)y * image->width + x;
    size_t pixel_index = index * image->channels;
    // --- Data Bounds Check ---
    // Ensure all accessed channels fit within the image's pixel dimensions.
    size_t data_size = (size_t)image->width * image->height * image->channels;
    if (image->channels <= 0 || pixel_index + image->channels > data_size) {
        // If index is out of bounds for the data buffer, return default info
        return info;
    }
    // --- End Bounds Checks ---

    // Get the color components based on the number of channels
    // Safely access channels if they exist
    const uint8_t *pixel = image->data + pixel_index;
    uint8_t red = image->channels >= 1 ? pixel[0] : 0;
    uint8_t green = image->channels >= 2 ? pixel[1] : 0;
    uint8_t blue = image->channels >= 3 ? pixel[2] : 0;
    // Calculate grayscale brightness using standard luminance weights
    // These weights are defined in image.h.
    uint64_t gray = (uint64_t)(GRAYSCALE_WEIGHT_R * red +
                               GRAYSCALE_WEIGHT_G * green +
                               GRAYSCALE_WEIGHT_B * blue);

    // --- Edge Detection Data Access ---
    // If edge detection was enabled and the edge magnitudes array was provided
    if (params->detect_edges && edges) {
        // Check if the current pixel's index is within the edge magnitudes array
        if (index < (size_t)image->width * image->height) {
            // Assign the pre-calculated edge magnitude for this pixel
            info.edge_magnitude = edges[index];
        } else {
            // Should not happen if edge magnitudes match the image size, but safer check
            info.edge_magnitude = 0.0f; // Default if index is somehow out of bounds
        }
    } else {
        // If edge detection is not enabled, use the calculated brightness value
        info.brightness = gray;
    }
    // --- End Edge Detection Data Access ---

    // If color output is enabled and the image has enough channels (at least 3 for RGB)
    if (params->color && image->channels >= 3) {
        // Store the pixel's color values directly
        info.color[0] = red;
        info.color[1] = green;
        info.color[2] = blue;
    }
    // The struct now holds info for this specific pixel
    return info;
}

// Select an ASCII character from the character set based on brightness or edge magnitude
// The character is chosen by mapping the pixel value (0-255) to the character set size
static char selectAsciiChar(const PixelInfo *pixel, const AsciiArtParams *params,
                            size_t character_count)
{
    uint64_t value; // Value used for character selection (brightness or edge magnitude)

    // Determine which value to use based on parameters
    if (params->detect_edges) {
        // Use edge magnitude if edge detection is enabled
        float magnitude = pixel->edge_magnitude;
        // Apply brightness boost and clamp between 0 and 255
        value = (uint64_t)fminf(fmaxf(magnitude * params->brightness_boost, 0.0f), 255.0f);
    } else {
        // Use brightness if edge detection is not enabled
        uint64_t brightness = pixel->brightness;
        // Apply brightness boost and clamp between 0 and 255
        float boosted_brightness = (float)brightness * params->brightness_boost;
        value = (uint64_t)fminf(fmaxf(boosted_brightness, 0.0f), 255.0f);
    }

    // Handle the case where the value is 0. Map to the first character unless inverted.
    // The first character is typically space for brightness.
    if (value == 0 && !params->invert_color) {
        return params->ascii_chars[0]; // Return the first character (lowest brightness)
    } else if (value == 0 && params->invert_color) {
        // Handle the case where the value is 0 and inverted. Map to the last character.
        return params->ascii_chars[character_count - 1]; // Highest brightness in inverted scale
    }

    // Map the value (0-255) to an index in the ASCII character set
    // The index is proportional to the value relative to the 0-256 range
    size_t index = (value * character_count) / 256;

    // Ensure the calculated index is within the bounds of the character set array
    if (index >= character_count) index = character_count - 1;

    // Return the character at the selected index
    // If invert_color is true, select from the end of the character set
    if (params->invert_color) index = character_count - 1 - index;
    return params->ascii_chars[index];
}

static const char colored_row_end[] = "\033[0m\n";

static char *allocateTextGrid(int width, size_t rows, size_t cell_size,
                              size_t row_suffix, size_t *capacity)
{
    if (width <= 0 || rows == 0 || (size_t)width > (SIZE_MAX - row_suffix) / cell_size) {
        fprintf(stderr, "Invalid output dimensions.\n");
        return NULL;
    }
    size_t row_size = (size_t)width * cell_size + row_suffix;
    if (rows > (SIZE_MAX - 1) / row_size) {
        fprintf(stderr, "Text output is too large.\n");
        return NULL;
    }
    *capacity = rows * row_size + 1;
    char *text = malloc(*capacity);
    if (!text) fprintf(stderr, "Failed to allocate text output.\n");
    return text;
}

static int blockPixelRows(BlockShape shape)
{
    return shape == BLOCK_FULL ? 1 : 2;
}

static char *generateBlocks(const Image *image, BlockShape shape)
{
    const char *glyph;
    switch (shape) {
        case BLOCK_UPPER: glyph = "▀"; break;
        case BLOCK_LOWER: glyph = "▄"; break;
        case BLOCK_FULL: glyph = "█"; break;
        default:
            fprintf(stderr, "Invalid block shape.\n");
            return NULL;
    }
    int pixel_rows = blockPixelRows(shape);
    int foreground = shape == BLOCK_LOWER ? 1 : 0;
    int background = shape == BLOCK_UPPER ? 1 : 0;
    const size_t cell_size = sizeof("\033[38;2;255;255;255m\033[48;2;255;255;255m▀") - 1;
    const size_t row_suffix = sizeof(colored_row_end) - 1;
    if (image->height <= 0 || !image->data || image->channels < 1 || image->channels > 4) {
        fprintf(stderr, "Invalid block image.\n");
        return NULL;
    }
    size_t rows = (size_t)image->height / pixel_rows + (image->height % pixel_rows != 0);
    size_t capacity;
    char *text = allocateTextGrid(image->width, rows, cell_size, row_suffix, &capacity);
    if (!text) return NULL;
    char *cursor = text;
    for (size_t row = 0; row < rows; row++) {
        for (int x = 0; x < image->width; x++) {
            unsigned colors[2][3] = {{0}};
            for (int half = 0; half < pixel_rows; half++) {
                size_t y = row * pixel_rows + half;
                // Pad a missing lower pixel with black instead of reading past the image.
                if (y >= (size_t)image->height) continue;
                const uint8_t *pixel = image->data +
                    (y * image->width + x) * image->channels;
                for (int channel = 0; channel < 3; channel++) {
                    colors[half][channel] = pixel[image->channels < 3 ? 0 : channel];
                }
            }
            size_t remaining = capacity - (size_t)(cursor - text);
            int length = snprintf(cursor, remaining,
                                  "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um%s",
                                  colors[foreground][0], colors[foreground][1], colors[foreground][2],
                                  colors[background][0], colors[background][1], colors[background][2],
                                  glyph);
            if (length < 0 || (size_t)length >= remaining) {
                free(text);
                return NULL;
            }
            cursor += length;
        }
        memcpy(cursor, colored_row_end, row_suffix);
        cursor += row_suffix;
    }
    *cursor = '\0';
    return text;
}

static int renderPixelRows(const AsciiArtParams *params)
{
    return params->render_mode == RENDER_BLOCK ? blockPixelRows(params->block_shape) : 1;
}

static int resizeRenderImage(const Image *image, const AsciiArtParams *params,
                             Image *resized)
{
    int pixel_rows = renderPixelRows(params);
    if (params->auto_fit) {
        return resizeImageToTerminal(image, params->aspect_ratio, pixel_rows, resized);
    }
    float width = image->width * params->scale;
    float height = image->height * params->scale / (params->aspect_ratio / pixel_rows);
    if (!isfinite(width) || !isfinite(height) ||
        (double)width > INT_MAX || (double)height > INT_MAX) {
        fprintf(stderr, "Invalid scaled image dimensions.\n");
        return -1;
    }
    return resizeImageToSize(image, (int)fmaxf(width, 1.0f),
                             (int)fmaxf(height, 1.0f), resized);
}

// Generate ASCII art as a text string
// Iterates through each pixel of the image, calculates its info, selects a character,
// and formats the output string, including ANSI color codes if enabled.
char *generateAsciiText(const Image *image, const AsciiArtParams *params,
                       const float *edge_magnitudes)
{
    if (params->render_mode == RENDER_BLOCK) return generateBlocks(image, params->block_shape);
    // Determine if color output should be used (requires color flag and enough image channels)
    bool use_color = params->color && image->channels >= 3;
    const size_t colored_pixel_size = sizeof("\033[38;2;255;255;255m") - 1 + 1;
    const size_t colored_row_size = sizeof(colored_row_end) - 1;
    size_t pixel_size = use_color ? colored_pixel_size : 1;
    size_t row_suffix = use_color ? colored_row_size : 1;
    size_t character_count = strlen(params->ascii_chars);
    if (!character_count || image->height <= 0) {
        fprintf(stderr, "Invalid ASCII output dimensions or character set.\n");
        return NULL;
    }
    size_t capacity;
    char *text = allocateTextGrid(image->width, image->height, pixel_size, row_suffix, &capacity);
    if (!text) return NULL;
    char *cursor = text;
    // Iterate through each row (height) of the image
    for (int y = 0; y < image->height; y++) {
        // Iterate through each column (width) of the image
        for (int x = 0; x < image->width; x++) {
            // Get the information for the current pixel
            // Pass the edge magnitudes pointer
            PixelInfo pixel = getPixelInfo(image, x, y, params, edge_magnitudes);
            // Select the ASCII character corresponding to this pixel's info
            char ascii_char = selectAsciiChar(&pixel, params, character_count);

            // --- Color Formatting ---
            if (use_color) {
                // Get the color components from the pixel info
                // Append ANSI color escape code for 24-bit color (RGB)
                // Format: \033[38;2;R;G;Bm
                int length = snprintf(cursor, capacity - (size_t)(cursor - text),
                                      "\033[38;2;%u;%u;%um", (unsigned)pixel.color[0],
                                      (unsigned)pixel.color[1], (unsigned)pixel.color[2]);
                if (length < 0 || (size_t)length >= capacity - (size_t)(cursor - text)) {
                    free(text);
                    return NULL;
                }
                cursor += length;
            }
            // Append the selected ASCII character
            // If color is not enabled, just append the ASCII character
            *cursor++ = ascii_char;
        }
        // --- Color Reset and Newline ---
        // Reset color at the end of each line to prevent bleeding into the next line or prompt
        if (use_color) {
            memcpy(cursor, colored_row_end, row_suffix);
            cursor += row_suffix;
        } else {
            *cursor++ = '\n';
        }
    }
    // Return the complete generated ASCII text string
    *cursor = '\0';
    return text;
}

static char *generateRenderText(const Image *image, const AsciiArtParams *params)
{
    float *edges = NULL;
    if (params->detect_edges && params->render_mode == RENDER_ASCII) {
        edges = detectEdges(image);
        if (!edges) return NULL;
    }
    char *text = generateAsciiText(image, params, edges);
    free(edges);
    return text;
}

// Main function to process an image and generate ASCII art
// Handles loading, resizing, edge detection, text generation, and output
int processImage(const AsciiArtParams *params)
{
    Image image = {0};
    Image resized = {0};
    char *text = NULL;
    int result = -1;
    // Load the image from the specified input path
    if (loadImage(params->input_path, &image) != 0) goto cleanup;
    // --- Image Resizing and Aspect Ratio Adjustment ---
    // Check if auto-fit to terminal is enabled
    if (params->render_mode == RENDER_BLOCK || params->auto_fit) {
        if (resizeRenderImage(&image, params, &resized) != 0) goto cleanup;
    // Otherwise, apply scaling based on the scale parameter
    } else if (params->scale != 1.0f) {
        // Original size mode with optional scaling
        // Calculate target dimensions preserving original aspect ratio
        float width = image.width * params->scale;
        float height = image.height * params->scale / params->aspect_ratio;
        // Safety bounds checking
        const int maximum_dimension = 1000;
        const int minimum_dimension = 10;
        if (!isfinite(width) || !isfinite(height) ||
            width >= maximum_dimension + 1 || height >= maximum_dimension + 1) {
            fprintf(stderr, "Error: Scaled dimensions too large. Maximum 1000x1000.\n");
            result = 0;
            goto cleanup;
        }
        if (width < minimum_dimension || height < minimum_dimension) {
            fprintf(stderr, "Error: Scaled dimensions too small. Minimum 10x10.\n");
            result = 0;
            goto cleanup;
        }
        // Create new image structure with target dimensions
        resized.width = (int)width;
        resized.height = (int)height;
        resized.channels = image.channels;
        resized.data = malloc((size_t)resized.width * resized.height * resized.channels);
        if (!resized.data) {
            fprintf(stderr, "Failed to allocate resized image.\n");
            goto cleanup;
        }

        // Reproduce OpenCV INTER_LINEAR resizing with half-pixel coordinates and 11-bit weights.
        const int coefficient_scale = 1 << 11;
        const int horizontal_shift = 4;
        const int vertical_shift = 16;
        const int output_shift = 2;
        const int output_rounding = 1 << (output_shift - 1);
        const int half_scale_divisor = 4;
        double scale_x = (double)image.width / resized.width;
        double scale_y = (double)image.height / resized.height;
        bool half_scale = image.width == resized.width * 2 && image.height == resized.height * 2;
        for (int y = 0; y < resized.height; y++) {
            float source_y = (float)((y + 0.5) * scale_y - 0.5);
            int top = (int)floorf(source_y);
            float fraction_y = source_y - top;
            int bottom = top + 1;
            if (top < 0) top = 0;
            if (top >= image.height) top = image.height - 1;
            if (bottom < 0) bottom = 0;
            if (bottom >= image.height) bottom = image.height - 1;
            int weight_top = (int)lrintf((1.0f - fraction_y) * coefficient_scale);
            int weight_bottom = (int)lrintf(fraction_y * coefficient_scale);
            for (int x = 0; x < resized.width; x++) {
                float source_x = (float)((x + 0.5) * scale_x - 0.5);
                int left = (int)floorf(source_x);
                float fraction_x = source_x - left;
                if (left < 0) {
                    left = 0;
                    fraction_x = 0.0f;
                }
                if (left >= image.width - 1) {
                    left = image.width - 1;
                    fraction_x = 0.0f;
                }
                int right = left < image.width - 1 ? left + 1 : left;
                int weight_left = (int)lrintf((1.0f - fraction_x) * coefficient_scale);
                int weight_right = (int)lrintf(fraction_x * coefficient_scale);
                for (int channel = 0; channel < image.channels; channel++) {
                    int top_left = image.data[((size_t)top * image.width + left) * image.channels + channel];
                    int top_right = image.data[((size_t)top * image.width + right) * image.channels + channel];
                    int bottom_left = image.data[((size_t)bottom * image.width + left) * image.channels + channel];
                    int bottom_right = image.data[((size_t)bottom * image.width + right) * image.channels + channel];
                    int value;
                    if (half_scale) {
                        value = (top_left + top_right + bottom_left + bottom_right +
                                 half_scale_divisor / 2) / half_scale_divisor;
                    } else {
                        int row_top = top_left * weight_left + top_right * weight_right;
                        int row_bottom = bottom_left * weight_left + bottom_right * weight_right;
                        value = (((weight_top * (row_top >> horizontal_shift)) >> vertical_shift) +
                                 ((weight_bottom * (row_bottom >> horizontal_shift)) >> vertical_shift) +
                                 output_rounding) >> output_shift;
                    }
                    resized.data[((size_t)y * resized.width + x) * resized.channels + channel] = (uint8_t)value;
                }
            }
        }
    }
    if (resized.data) {
        freeImage(&image);
        image = resized;
        resized = (Image){0};
    }
    text = generateRenderText(&image, params);
    if (!text) goto cleanup;
    // --- Output Handling ---
    // If an output path is specified, save the ASCII text to a file
    if (params->output_path && params->output_path[0]) {
        result = saveOutputText(text, params->output_path);
    } else {
        // If no output path is specified, print the ASCII text to the console
        result = printf("\n%s\n", text) < 0 ? -1 : 0;
    }
cleanup:
    free(text);
    freeImage(&resized);
    freeImage(&image);
    return result;
}

// --- Video Processing Implementation ---

// Check if file is a supported video/GIF format
// Supports common video formats and animated GIFs
bool isVideoFile(const char *filename)
{
    // Check for supported video and GIF extensions
    static const char *extensions[] = {
        ".mp4", ".avi", ".mov", ".mkv", ".webm", ".gif", ".m4v", ".wmv", ".flv"
    };
    for (const char *start = filename; *start; start++) {
        for (size_t index = 0; index < sizeof(extensions) / sizeof(extensions[0]); index++) {
            size_t offset = 0;
            // Convert filename characters to lowercase for case-insensitive comparison
            while (extensions[index][offset] && start[offset] &&
                   tolower((unsigned char)start[offset]) == extensions[index][offset]) {
                offset++;
            }
            if (!extensions[index][offset]) return true;
        }
    }
    return false;
}

typedef enum {
    ROTATION_NONE = 0,
    ROTATION_CLOCKWISE = 90,
    ROTATION_HALF = 180,
    ROTATION_COUNTERCLOCKWISE = 270
} VideoRotation;

static VideoRotation videoRotation(const AVStream *stream)
{
    const int32_t *matrix = NULL;
#if LIBAVFORMAT_VERSION_MAJOR >= 61
    const AVPacketSideData *side_data = av_packet_side_data_get(
        stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (side_data && side_data->size >= sizeof(int32_t[3][3])) {
        matrix = (const int32_t *)side_data->data;
    }
#else
    matrix = (const int32_t *)av_stream_get_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX, NULL);
#endif
    if (!matrix) return ROTATION_NONE;
    double angle = -av_display_rotation_get(matrix);
    if (!isfinite(angle)) return ROTATION_NONE;
    const int full_rotation = 360;
    int degrees = ((int)lrint(angle) % full_rotation + full_rotation) % full_rotation;
    switch (degrees) {
        case ROTATION_CLOCKWISE: return ROTATION_CLOCKWISE;
        case ROTATION_HALF: return ROTATION_HALF;
        case ROTATION_COUNTERCLOCKWISE: return ROTATION_COUNTERCLOCKWISE;
        default: return ROTATION_NONE;
    }
}

typedef struct {
    struct SwsContext *context;
    int width;
    int height;
    enum AVPixelFormat format;
    int colorspace;
    int source_range;
} VideoConverter;

static int configureConverter(const AVFrame *frame, VideoConverter *converter)
{
    // Geometry and format changes can recreate a context at the same address.
    bool context_changed = !converter->context || converter->width != frame->width ||
                           converter->height != frame->height || converter->format != frame->format;
    if (context_changed) {
        converter->context = sws_getCachedContext(converter->context, frame->width, frame->height,
                                                  (enum AVPixelFormat)frame->format,
                                                  frame->width, frame->height, AV_PIX_FMT_RGB24,
                                                  SWS_BILINEAR, NULL, NULL, NULL);
        if (!converter->context) return AVERROR(ENOMEM);
    }
    int colorspace = SWS_CS_DEFAULT;
    switch (frame->colorspace) {
        case AVCOL_SPC_BT709: colorspace = SWS_CS_ITU709; break;
        case AVCOL_SPC_FCC: colorspace = SWS_CS_FCC; break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: colorspace = SWS_CS_ITU601; break;
        case AVCOL_SPC_SMPTE240M: colorspace = SWS_CS_SMPTE240M; break;
        case AVCOL_SPC_BT2020_NCL: colorspace = SWS_CS_BT2020; break;
        default: break;
    }
    int source_range = frame->color_range == AVCOL_RANGE_JPEG;
    if (frame->color_range == AVCOL_RANGE_UNSPECIFIED) {
        const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(frame->format);
        source_range = descriptor && ((descriptor->flags & AV_PIX_FMT_FLAG_RGB) ||
                                      !strncmp(descriptor->name, "yuvj", 4));
    }
    if (context_changed || converter->colorspace != colorspace ||
        converter->source_range != source_range) {
        const int *coefficients = sws_getCoefficients(colorspace);
        const int full_range = 1;
        const int fixed_point_unity = 1 << 16;
        int result = sws_setColorspaceDetails(converter->context, coefficients, source_range,
                                              coefficients, full_range, 0,
                                              fixed_point_unity, fixed_point_unity);
        if (result < 0) return result;
        converter->width = frame->width;
        converter->height = frame->height;
        converter->format = frame->format;
        converter->colorspace = colorspace;
        converter->source_range = source_range;
    }
    return 0;
}

// Convert a decoded frame and apply its display rotation.
static int frameToImage(const AVFrame *frame, VideoConverter *converter, Image *image,
                        VideoRotation rotation)
{
    int result = configureConverter(frame, converter);
    if (result < 0) return result;
    uint8_t *pixels[4] = {0};
    int strides[4] = {0};
    result = av_image_alloc(pixels, strides, frame->width, frame->height, AV_PIX_FMT_RGB24, 32);
    if (result < 0) return result;
    result = sws_scale(converter->context, (const uint8_t *const *)frame->data, frame->linesize,
                       0, frame->height, pixels, strides);
    if (result < 0) {
        av_freep(&pixels[0]);
        return result;
    }
    // Calculate total data size, excluding FFmpeg's row-alignment padding
    size_t row_size = (size_t)frame->width * 3;
    image->data = malloc(row_size * frame->height);
    if (!image->data) {
        av_freep(&pixels[0]);
        return AVERROR(ENOMEM);
    }
    // Set basic image properties
    image->width = frame->width;
    image->height = frame->height;
    if (rotation == ROTATION_CLOCKWISE || rotation == ROTATION_COUNTERCLOCKWISE) {
        image->width = frame->height;
        image->height = frame->width;
    }
    image->channels = 3;
    for (int y = 0; y < frame->height; y++) {
        const uint8_t *row = pixels[0] + (size_t)y * strides[0];
        if (rotation == ROTATION_NONE) {
            memcpy(image->data + (size_t)y * row_size, row, row_size);
            continue;
        }
        for (int x = 0; x < frame->width; x++) {
            int target_x;
            int target_y;
            switch (rotation) {
                case ROTATION_CLOCKWISE:
                    target_x = frame->height - 1 - y;
                    target_y = x;
                    break;
                case ROTATION_COUNTERCLOCKWISE:
                    target_x = y;
                    target_y = frame->width - 1 - x;
                    break;
                default:
                    target_x = frame->width - 1 - x;
                    target_y = frame->height - 1 - y;
                    break;
            }
            memcpy(image->data + ((size_t)target_y * image->width + target_x) * image->channels,
                   row + (size_t)x * image->channels, image->channels);
        }
    }
    av_freep(&pixels[0]);
    return 0;
}

// Apply the existing processing pipeline and render one frame with artifact cleanup.
static int renderVideoFrame(const AVFrame *frame, VideoConverter *converter,
                            const AsciiArtParams *params, TerminalSize *previous,
                            VideoRotation rotation)
{
    Image image = {0};
    Image resized = {0};
    char *text = NULL;
    // Convert and process frame
    int result = frameToImage(frame, converter, &image, rotation);
    if (result < 0) goto cleanup;
    result = resizeRenderImage(&image, params, &resized);
    if (result != 0) goto cleanup;
    text = generateRenderText(&resized, params);
    if (!text) {
        result = -1;
        goto cleanup;
    }
    int pixel_rows = renderPixelRows(params);
    int rendered_height = resized.height / pixel_rows + (resized.height % pixel_rows != 0);
    // Frame rendering with cleanup
    fputs("\033[1;1H", stdout); // Go to top-left

    // Clean up previous frame artifacts
    if (previous->height > 0 || previous->width > 0) {
        // Clear extra lines if frame got shorter
        for (int line = rendered_height + 1; line <= previous->height; line++) {
            printf("\033[%d;1H\033[K", line);
        }
        // Clear extra columns if frame got narrower
        if (resized.width < previous->width) {
            int height = rendered_height > previous->height ? rendered_height : previous->height;
            for (int line = 1; line <= height; line++) {
                printf("\033[%d;%dH\033[K", line, resized.width + 1);
            }
        }
        fputs("\033[1;1H", stdout);
    }
    // Display the frame
    if (fputs(text, stdout) == EOF || fflush(stdout) == EOF) result = -1;

    // Update dimensions for next iteration
    previous->width = resized.width;
    previous->height = rendered_height;
cleanup:
    free(text);
    freeImage(&resized);
    freeImage(&image);
    return result;
}

// Process video/GIF file with smooth playback and proper frame buffer management
int processVideo(const char *path, const AsciiArtParams *params, int frame_delay)
{
    AVFormatContext *format = NULL;
    AVCodecContext *decoder = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    VideoConverter converter = {0};
    bool terminal_active = false;
    TerminalSize previous = {0};
    // Open the media and select its video stream.
    int result = avformat_open_input(&format, path, NULL, NULL);
    if (result < 0) goto cleanup;
    if ((result = avformat_find_stream_info(format, NULL)) < 0) goto cleanup;
    const AVCodec *codec = NULL;
    int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_index < 0) {
        result = stream_index;
        goto cleanup;
    }
    VideoRotation rotation = videoRotation(format->streams[stream_index]);
    decoder = avcodec_alloc_context3(codec);
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!decoder || !packet || !frame) {
        result = AVERROR(ENOMEM);
        goto cleanup;
    }
    if ((result = avcodec_parameters_to_context(decoder, format->streams[stream_index]->codecpar)) < 0 ||
        (result = avcodec_open2(decoder, codec, NULL)) < 0) goto cleanup;
    // Match the detected frame rate unless a custom delay was supplied.
    AVRational rate = av_guess_frame_rate(format, format->streams[stream_index], NULL);
    double fps = av_q2d(rate);
    int calculated_delay = DEFAULT_FRAME_DELAY;
    if (isfinite(fps) && fps > 0 && 1000.0 / fps <= INT_MAX) {
        calculated_delay = (int)(1000.0 / fps);
    }
    int actual_delay = frame_delay == DEFAULT_FRAME_DELAY ? calculated_delay : frame_delay;
    // Terminal initialization
    fputs("\033c", stdout); // Full reset
    fputs("\033[?1049h", stdout); // Enter alternate screen
    fputs("\033[2J\033[1;1H\033[?25l", stdout); // Clear screen, position cursor, hide cursor
    fputs("\033[?1000h", stdout); // Capture mouse event
    fflush(stdout);
    terminal_active = true;
    int64_t last_frame_time = av_gettime_relative();
    // Decode packets, including buffered frames drained at the end of the stream.
    for (;;) {
        result = av_read_frame(format, packet);
        bool draining = result == AVERROR_EOF;
        if (result < 0 && !draining) goto cleanup;
        if (!draining && packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }
        result = avcodec_send_packet(decoder, draining ? NULL : packet);
        av_packet_unref(packet);
        if (result < 0) goto cleanup;
        while ((result = avcodec_receive_frame(decoder, frame)) >= 0) {
            result = renderVideoFrame(frame, &converter, params, &previous, rotation);
            av_frame_unref(frame);
            if (result != 0) goto cleanup;
            // Timing control
            int64_t elapsed = av_gettime_relative() - last_frame_time;
            int64_t remaining = (int64_t)actual_delay * 1000 - elapsed;
            while (remaining > 0) {
                unsigned int sleep_time = remaining > INT_MAX ? INT_MAX : (unsigned int)remaining;
                av_usleep(sleep_time);
                remaining = (int64_t)actual_delay * 1000 - (av_gettime_relative() - last_frame_time);
            }
            last_frame_time = av_gettime_relative();
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) goto cleanup;
        if (draining) break;
    }
    result = 0;
cleanup:
    // Restore and exit
    if (terminal_active) {
        fputs("\033[?1000l", stdout); // Disable mouse capture
        fputs("\033[?25h", stdout); // Show cursor
        fputs("\033[?1049l", stdout); // Exit alternate screen
        fflush(stdout);
    }
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        fprintf(stderr, "Failed to process video/GIF file: %s - %s\n", path, message);
    }
    // Release the conversion context, frame, packet, decoder and input stream.
    sws_freeContext(converter.context);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    return result;
}
