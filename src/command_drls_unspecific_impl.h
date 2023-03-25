#ifndef COMMAND_DRLS_UNSPECIFIC_IMPL_H
#define COMMAND_DRLS_UNSPECIFIC_IMPL_H

#include <stdint.h>

#include "arrays.h"

#define DRLS_UNSPECIFIC_STAGE_INITIAL 0
#define DRLS_UNSPECIFIC_STAGE_COUNT 1
#define DRLS_UNSPECIFIC_STAGE_PREPARE 2
#define DRLS_UNSPECIFIC_STAGE_RETRIEVE 3
#define DRLS_UNSPECIFIC_STAGE_SUBMIT 4

typedef uint8_t drls_unspecific_stage_t;

typedef struct {
    dynamic_array_t *datarefs;
    dynamic_array_t *dataref_infos;
    int count;
    int num_prepared;
    drls_unspecific_stage_t stage;
} drls_unspecific_t;

#endif
