#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdlib.h>

#include <XPLMDataAccess.h>

#include "arrays.h"

#define XPRC_TYPE_INT "int"
#define XPRC_TYPE_INT_LENGTH (sizeof(XPRC_TYPE_INT)-1)
#define XPRC_TYPE_FLOAT "float"
#define XPRC_TYPE_FLOAT_LENGTH (sizeof(XPRC_TYPE_FLOAT)-1)
#define XPRC_TYPE_DOUBLE "double"
#define XPRC_TYPE_DOUBLE_LENGTH (sizeof(XPRC_TYPE_DOUBLE)-1)
#define XPRC_TYPE_INT_ARRAY "int[]"
#define XPRC_TYPE_INT_ARRAY_LENGTH (sizeof(XPRC_TYPE_INT_ARRAY)-1)
#define XPRC_TYPE_FLOAT_ARRAY "float[]"
#define XPRC_TYPE_FLOAT_ARRAY_LENGTH (sizeof(XPRC_TYPE_FLOAT_ARRAY)-1)
#define XPRC_TYPE_BLOB "blob"
#define XPRC_TYPE_BLOB_LENGTH (sizeof(XPRC_TYPE_BLOB)-1)

#define XPRC_TYPE_SEPARATOR ","
#define XPRC_TYPE_SEPARATOR_LENGTH 1

XPLMDataTypeID xprc_parse_type(char *s, int count);
char* xprc_encode_array(XPLMDataTypeID type, dynamic_array_t *arr);
char* xprc_encode_value(XPLMDataTypeID type, void *value, size_t value_size);
char* xprc_encode_types(XPLMDataTypeID types);

#endif
