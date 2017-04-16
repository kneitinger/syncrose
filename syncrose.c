/*
 * syncrose.c
 *
 * Copyright (c) 2017 Kyle Kneitiner <kyle@kneit.in>
 *
 * This software is licensed under the 3-Clause BSD License
 * For license details see syncrose/LICENSE
 * or https://opensource.org/licenses/BSD-3-Clause
 *
 */

#include "syncrose.h"

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    Syncrose* syncrose = (Syncrose*)malloc(sizeof(Syncrose));

    return (LV2_Handle)syncrose;
}

static void
connect_port(LV2_Handle instance,
        uint32_t   port,
        void*      data)
{
    Syncrose* syncrose = (Syncrose*)instance;

    switch ((PortIndex)port) {
        case OUTPUT_LEFT:
            syncrose->output_left = (float*)data;
            break;
        case OUTPUT_RIGHT:
            syncrose->output_right = (float*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
}



static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
}


static void
run(LV2_Handle instance, uint32_t n_samples)
{
    const Syncrose* syncrose = (const Syncrose*)instance;

    float* const output_left  = syncrose->output_left;
    float* const output_right = syncrose->output_right;

    for (uint32_t pos = 0; pos < n_samples; pos++) {
        output_left[pos]  = 0;
        output_right[pos] = 0;
    }
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    SYNCROSE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
        case 0:  return &descriptor;
        default: return NULL;
    }
}
