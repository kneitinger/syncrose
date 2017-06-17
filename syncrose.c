#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef __cplusplus
#    include <stdbool.h>
#endif

#include <sndfile.h>

#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#include "./uris.h"

enum {
    SYNCROSE_CONTROL = 0,
    SYNCROSE_NOTIFY  = 1,
    SYNCROSE_OUT     = 2,
    SYNCROSE_START   = 3,
    SYNCROSE_STEP   = 4
};

static const char* default_sample_file = "clip.wav";

typedef struct {
    SF_INFO  info;      // Info about sample from sndfile
    float*   data;      // Sample data in float
    char*    path;      // Path of file
    uint32_t path_len;  // Length of path
} Sample;

typedef struct {
    // Features
    LV2_URID_Map*        map;
    LV2_Worker_Schedule* schedule;
    LV2_Log_Log*         log;
    LV2_Atom_Forge forge;

    // Logger convenience API
    LV2_Log_Logger logger;

    Sample* sample;
    bool    sample_changed;

    const LV2_Atom_Sequence* control_port;
    LV2_Atom_Sequence*       notify_port;
    float*                   output_port;
    float*                   start_port;
    float*                     step_port;

    // Forge frame for notify port (for writing worker replies)
    LV2_Atom_Forge_Frame notify_frame;

    // URIs
    SyncroseURIs uris;

    // Position in run() if sample is already in progress
    uint32_t frame_offset;

    // Playback state
    float      gain;
    sf_count_t frame;
    sf_count_t step;
    sf_count_t start;
    bool       play;
} Syncrose;

typedef struct {
    LV2_Atom atom;
    Sample*  sample;
} SampleMessage;

static Sample*
load_sample(Syncrose* self, const char* path)
{
    const size_t path_len = strlen(path);

    lv2_log_trace(&self->logger, "Loading sample %s\n", path);

    Sample* const  sample  = (Sample*)malloc(sizeof(Sample));
    SF_INFO* const info    = &sample->info;
    SNDFILE* const sndfile = sf_open(path, SFM_READ, info);

    if (!sndfile || !info->frames || (info->channels != 1)) {
        lv2_log_error(&self->logger, "Failed to open sample '%s'\n", path);
        free(sample);
        return NULL;
    }

    // Read data
    float* const data = malloc(sizeof(float) * info->frames);
    if (!data) {
        lv2_log_error(&self->logger, "Failed to allocate memory for sample\n");
        return NULL;
    }
    sf_seek(sndfile, 0ul, SEEK_SET);
    sf_read_float(sndfile, data, info->frames);
    sf_close(sndfile);

    // Fill sample struct and return it
    sample->data     = data;
    sample->path     = (char*)malloc(path_len + 1);
    sample->path_len = (uint32_t)path_len;
    memcpy(sample->path, path, path_len + 1);

    return sample;
}

static void
free_sample(Syncrose* self, Sample* sample)
{
    if (sample) {
        lv2_log_trace(&self->logger, "Freeing %s\n", sample->path);
        free(sample->path);
        free(sample->data);
        free(sample);
    }
}

// Thread for non-realtime file loading
static LV2_Worker_Status
work(LV2_Handle                  instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
    Syncrose*        self = (Syncrose*)instance;
    const LV2_Atom* atom = (const LV2_Atom*)data;
    if (atom->type == self->uris.freeSample) {
        // Free old sample
        const SampleMessage* msg = (const SampleMessage*)data;
        free_sample(self, msg->sample);
    } else {
        // Handle set message (load sample).
        const LV2_Atom_Object* obj = (const LV2_Atom_Object*)data;

        // Get file path from message
        const LV2_Atom* file_path = read_set_file(&self->uris, obj);
        if (!file_path) {
            return LV2_WORKER_ERR_UNKNOWN;
        }

        // Load sample.
        Sample* sample = load_sample(self, LV2_ATOM_BODY_CONST(file_path));
        if (sample) {
            // Loaded sample, send it to run() to be applied.
            respond(handle, sizeof(sample), &sample);
        }
    }

    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response(LV2_Handle  instance,
              uint32_t    size,
              const void* data)
{
    Syncrose* self = (Syncrose*)instance;

    SampleMessage msg = { { sizeof(Sample*), self->uris.freeSample },
                          self->sample };

    // Send a message to the worker to free the current sample
    self->schedule->schedule_work(self->schedule->handle, sizeof(msg), &msg);

    // Install the new sample
    self->sample = *(Sample*const*)data;

    // Send a notification that we're using a new sample.
    lv2_atom_forge_frame_time(&self->forge, self->frame_offset);
    write_set_file(&self->forge, &self->uris,
                   self->sample->path,
                   self->sample->path_len);

    return LV2_WORKER_SUCCESS;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
    Syncrose* self = (Syncrose*)instance;
    switch (port) {
    case SYNCROSE_CONTROL:
        self->control_port = (const LV2_Atom_Sequence*)data;
        break;
    case SYNCROSE_NOTIFY:
        self->notify_port = (LV2_Atom_Sequence*)data;
        break;
    case SYNCROSE_OUT:
        self->output_port = (float*)data;
        break;
    case SYNCROSE_START:
        self->start_port = (float*)data;
        break;
    case SYNCROSE_STEP:
        self->step_port = (float*)data;
        break;
    default:
        break;
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               path,
            const LV2_Feature* const* features)
{
    // Allocate and initialise instance structure.
    Syncrose* self = (Syncrose*)malloc(sizeof(Syncrose));
    if (!self) {
        return NULL;
    }
    memset(self, 0, sizeof(Syncrose));

    // Get host features
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->schedule = (LV2_Worker_Schedule*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            self->log = (LV2_Log_Log*)features[i]->data;
        }
    }
    if (!self->map) {
        lv2_log_error(&self->logger, "Missing feature urid:map\n");
        goto fail;
    } else if (!self->schedule) {
        lv2_log_error(&self->logger, "Missing feature work:schedule\n");
        goto fail;
    }

    // Map URIs and initialise forge/logger
    map_sampler_uris(self->map, &self->uris);
    lv2_atom_forge_init(&self->forge, self->map);
    lv2_log_logger_init(&self->logger, self->map, self->log);

    // Load the default sample file
    const size_t path_len    = strlen(path);
    const size_t file_len    = strlen(default_sample_file);
    const size_t len         = path_len + file_len;
    char*        sample_path = (char*)malloc(len + 1);
    snprintf(sample_path, len + 1, "%s%s", path, default_sample_file);
    self->sample = load_sample(self, sample_path);
    free(sample_path);

    self->start = 0;
    self->step  = self->sample->info.frames;

    return (LV2_Handle)self;

fail:
    free(self);
    return 0;
}

static void
cleanup(LV2_Handle instance)
{
    Syncrose* self = (Syncrose*)instance;
    free_sample(self, self->sample);
    free(self);
}

#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)

static void
run(LV2_Handle instance,
    uint32_t   sample_count)
{
    Syncrose*     self        = (Syncrose*)instance;
    SyncroseURIs* uris        = &self->uris;
    sf_count_t   start_frame = 0;
    sf_count_t   pos         = 0;
    float*       output      = self->output_port;


    // Set up forge to write directly to notify output port.
    const uint32_t notify_capacity = self->notify_port->atom.size;
    lv2_atom_forge_set_buffer(&self->forge,
                              (uint8_t*)self->notify_port,
                              notify_capacity);

    // Start a sequence in the notify output port.
    lv2_atom_forge_sequence_head(&self->forge, &self->notify_frame, 0);

    // Send update to UI if sample has changed due to state restore
    if (self->sample_changed) {
        lv2_atom_forge_frame_time(&self->forge, 0);
        write_set_file(&self->forge, &self->uris,
                       self->sample->path,
                       self->sample->path_len);
        self->sample_changed = false;
    }

    // Read incoming events
    LV2_ATOM_SEQUENCE_FOREACH(self->control_port, ev) {
        self->frame_offset = ev->time.frames;

        if (ev->body.type == uris->midi_Event) {
            const uint8_t* const msg = (const uint8_t*)(ev + 1);
            switch (lv2_midi_message_type(msg)) {
            case LV2_MIDI_MSG_NOTE_ON:
                start_frame = ev->time.frames;
                self->frame = (long)(*(self->start_port)/127 * (float)self->sample->info.frames);
                self->play  = true;
                break;
            case LV2_MIDI_MSG_NOTE_OFF:
                self->play  = false;
                break;
            default:
                break;
            }
        } else if (lv2_atom_forge_is_object_type(&self->forge, ev->body.type)) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == uris->patch_Set) {
                // Get the property and value of the set message
                const LV2_Atom* property = NULL;
                const LV2_Atom* value    = NULL;
                lv2_atom_object_get(obj,
                                    uris->patch_property, &property,
                                    uris->patch_value,    &value,
                                    0);
                if (!property) {
                    lv2_log_error(&self->logger,
                                  "patch:Set message with no property\n");
                    continue;
                } else if (property->type != uris->atom_URID) {
                    lv2_log_error(&self->logger,
                                  "patch:Set property is not a URID\n");
                    continue;
                }

                const uint32_t key = ((const LV2_Atom_URID*)property)->body;
                if (key == uris->sample) {
                    // Sample change, send it to the worker.
                    lv2_log_trace(&self->logger, "Queueing set message\n");
                    self->schedule->schedule_work(self->schedule->handle,
                                                  lv2_atom_total_size(&ev->body),
                                                  &ev->body);
                } else if (key == uris->param_gain) {
                    // Gain change
                    if (value->type == uris->atom_Float) {
                        self->gain = DB_CO(((LV2_Atom_Float*)value)->body);
                    }
                }
            } else if (obj->body.otype == uris->patch_Get) {
                // Received a get message, emit our state (probably to UI)
                lv2_log_trace(&self->logger, "Responding to get request\n");
                lv2_atom_forge_frame_time(&self->forge, self->frame_offset);
                write_set_file(&self->forge, &self->uris,
                               self->sample->path,
                               self->sample->path_len);
            } else {
                lv2_log_trace(&self->logger,
                              "Unknown object type %d\n", obj->body.otype);
            }
        } else {
            lv2_log_trace(&self->logger,
                          "Unknown event type %d\n", ev->body.type);
        }
    }

    // Render the sample (possibly already in progress)
    if (self->play) {
        uint32_t       f  = self->frame;
        sf_count_t   calc_start  = (long)(*(self->start_port)/127 * (float)self->sample->info.frames);
        sf_count_t   calc_step   = (long)(*(self->step_port)*10);
        const uint32_t lf = self->sample->info.frames < calc_start + calc_step ?
            self->sample->info.frames : calc_start + calc_step;

        for (pos = 0; pos < start_frame; ++pos) {
            output[pos] = 0;
        }

        for (; pos < sample_count && f < lf; ++pos, ++f) {
            output[pos] = self->sample->data[f] * 1;
        }

        self->frame = f;

        if (f == lf) {
            self->frame = calc_start;
        }
    }

    // Add zeros to end if sample not long enough (or not playing)
    for (; pos < sample_count; ++pos) {
        output[pos] = 0.0f;
    }
}

static LV2_State_Map_Path*
get_map_path(LV2_Log_Logger* logger, const LV2_Feature* const* features)
{
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_STATE__mapPath)) {
            return (LV2_State_Map_Path*)features[i]->data;
        }
    }
    lv2_log_error(logger, "Missing map:path feature\n");
    return NULL;
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
    Syncrose* self = (Syncrose*)instance;
    if (!self->sample) {
        return LV2_STATE_SUCCESS;
    }

    LV2_State_Map_Path* map_path = get_map_path(&self->logger, features);
    if (!map_path) {
        return LV2_STATE_ERR_NO_FEATURE;
    }

    // Map absolute sample path to an abstract state path
    char* apath = map_path->abstract_path(map_path->handle, self->sample->path);

    // Path storage
    store(handle,
          self->uris.sample,
          apath,
          strlen(apath) + 1,
          self->uris.atom_Path,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    free(apath);
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle            handle,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
    Syncrose* self = (Syncrose*)instance;

    // Obtain syncrose:sample
    size_t      size;
    uint32_t    type;
    uint32_t    valflags;
    const void* value = retrieve(handle, self->uris.sample,
                                 &size, &type, &valflags);
    if (!value) {
        lv2_log_error(&self->logger, "Missing syncrose:sample\n");
        return LV2_STATE_ERR_NO_PROPERTY;
    } else if (type != self->uris.atom_Path) {
        lv2_log_error(&self->logger, "Non-path syncrose:sample\n");
        return LV2_STATE_ERR_BAD_TYPE;
    }

    LV2_State_Map_Path* map_path = get_map_path(&self->logger, features);
    if (!map_path) {
        return LV2_STATE_ERR_NO_FEATURE;
    }

    const char* apath = (const char*)value;
    char*       path  = map_path->absolute_path(map_path->handle, apath);

    lv2_log_trace(&self->logger, "Restoring file %s\n", path);
    free_sample(self, self->sample);
    self->sample = load_sample(self, path);
    self->sample_changed = true;

    return LV2_STATE_SUCCESS;
}

static const void*
extension_data(const char* uri)
{
    static const LV2_State_Interface  state  = { save, restore };
    static const LV2_Worker_Interface worker = { work, work_response, NULL };
    if (!strcmp(uri, LV2_STATE__interface)) {
        return &state;
    } else if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    return NULL;
}

static const LV2_Descriptor descriptor = {
    SYNCROSE_URI,
    instantiate,
    connect_port,
    NULL,  // activate,
    run,
    NULL,  // deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0:
        return &descriptor;
    default:
        return NULL;
    }
}
