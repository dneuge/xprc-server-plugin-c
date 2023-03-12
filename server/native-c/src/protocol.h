#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdlib.h>

#include <XPLMDataAccess.h>

#include "arrays.h"

XPLMDataTypeID xprc_parse_type(char *s, int count);
char* xprc_encode_array(XPLMDataTypeID type, dynamic_array_t *arr);
char* xprc_encode_value(XPLMDataTypeID type, void *value, size_t value_size);

#endif
