#ifndef PIXCII_EDGE_DETECTION_H
#define PIXCII_EDGE_DETECTION_H

#include "image.h"

// --- Function Declarations ---

// Performs edge detection on an image using the Sobel operator.
// image: The input Image struct.
// Returns: A caller-owned array of floats where each element is the edge magnitude
// for the corresponding pixel, or NULL on allocation failure.
float *detectEdges(const Image *image);

#endif
