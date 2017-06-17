#ifndef SYNCROSE_URIS_H
#define SYNCROSE_URIS_H

#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/parameters/parameters.h"

#define SYNCROSE_URI          "http://kneit.in/plugins/syncrose"
#define SYNCROSE__sample      SYNCROSE_URI "#sample"
#define SYNCROSE__applySample SYNCROSE_URI "#applySample"
#define SYNCROSE__freeSample  SYNCROSE_URI "#freeSample"

typedef struct {
	LV2_URID atom_Float;
	LV2_URID atom_Path;
	LV2_URID atom_Resource;
	LV2_URID atom_Sequence;
	LV2_URID atom_URID;
	LV2_URID atom_eventTransfer;
	LV2_URID applySample;
	LV2_URID sample;
	LV2_URID freeSample;
	LV2_URID midi_Event;
	LV2_URID param_gain;
	LV2_URID patch_Get;
	LV2_URID patch_Set;
	LV2_URID patch_property;
	LV2_URID patch_value;
} SyncroseURIs;

static inline void
map_sampler_uris(LV2_URID_Map* map, SyncroseURIs* uris)
{
	uris->atom_Float         = map->map(map->handle, LV2_ATOM__Float);
	uris->atom_Path          = map->map(map->handle, LV2_ATOM__Path);
	uris->atom_Resource      = map->map(map->handle, LV2_ATOM__Resource);
	uris->atom_Sequence      = map->map(map->handle, LV2_ATOM__Sequence);
	uris->atom_URID          = map->map(map->handle, LV2_ATOM__URID);
	uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	uris->applySample     = map->map(map->handle, SYNCROSE__applySample);
	uris->freeSample      = map->map(map->handle, SYNCROSE__freeSample);
	uris->sample          = map->map(map->handle, SYNCROSE__sample);
	uris->midi_Event         = map->map(map->handle, LV2_MIDI__MidiEvent);
	uris->param_gain         = map->map(map->handle, LV2_PARAMETERS__gain);
	uris->patch_Get          = map->map(map->handle, LV2_PATCH__Get);
	uris->patch_Set          = map->map(map->handle, LV2_PATCH__Set);
	uris->patch_property     = map->map(map->handle, LV2_PATCH__property);
	uris->patch_value        = map->map(map->handle, LV2_PATCH__value);
}

static inline LV2_Atom*
write_set_file(LV2_Atom_Forge*    forge,
               const SyncroseURIs* uris,
               const char*        filename,
               const uint32_t     filename_len)
{
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* set = (LV2_Atom*)lv2_atom_forge_object(
		forge, &frame, 0, uris->patch_Set);

	lv2_atom_forge_key(forge, uris->patch_property);
	lv2_atom_forge_urid(forge, uris->sample);
	lv2_atom_forge_key(forge, uris->patch_value);
	lv2_atom_forge_path(forge, filename, filename_len);

	lv2_atom_forge_pop(forge, &frame);

	return set;
}

static inline const LV2_Atom*
read_set_file(const SyncroseURIs*     uris,
              const LV2_Atom_Object* obj)
{
	if (obj->body.otype != uris->patch_Set) {
		fprintf(stderr, "Ignoring unknown message type %d\n", obj->body.otype);
		return NULL;
	}

	/* Get property URI. */
	const LV2_Atom* property = NULL;
	lv2_atom_object_get(obj, uris->patch_property, &property, 0);
	if (!property) {
		fprintf(stderr, "Malformed set message has no body.\n");
		return NULL;
	} else if (property->type != uris->atom_URID) {
		fprintf(stderr, "Malformed set message has non-URID property.\n");
		return NULL;
	} else if (((const LV2_Atom_URID*)property)->body != uris->sample) {
		fprintf(stderr, "Set message for unknown property.\n");
		return NULL;
	}

	/* Get value. */
	const LV2_Atom* file_path = NULL;
	lv2_atom_object_get(obj, uris->patch_value, &file_path, 0);
	if (!file_path) {
		fprintf(stderr, "Malformed set message has no value.\n");
		return NULL;
	} else if (file_path->type != uris->atom_Path) {
		fprintf(stderr, "Set message value is not a Path.\n");
		return NULL;
	}

	return file_path;
}

#endif  /* SYNCROSE_URIS_H */
