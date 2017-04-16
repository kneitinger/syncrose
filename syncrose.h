/*
 * syncrose.h
 *
 * Copyright (c) 2017 Kyle Kneitiner <kyle@kneit.in>
 *
 * This software is licensed under the 3-Clause BSD License
 * For license details see syncrose/LICENSE
 * or https://opensource.org/licenses/BSD-3-Clause
 *
 */

#ifndef SYNCROSE_H
#define SYNCROSE_H


#include <math.h>
#include <stdlib.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define SYNCROSE_URI "http://kneit.in/plugins/syncrose"


typedef enum {
    OUTPUT_LEFT  = 0,
    OUTPUT_RIGHT = 1
} PortIndex;

typedef struct {
    // Port buffers
    float*  output_left;
    float*  output_right;
} Syncrose;

#endif
