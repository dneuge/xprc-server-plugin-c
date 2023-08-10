#ifndef PROTOCOL_H
#define PROTOCOL_H

/**
 * @file protocol.h constants and helper functions related to the XPRC protocol
 */

#include <stdlib.h>

#include <XPLMDataAccess.h>

#include "arrays.h"

/// type name for an integer value in XPRC
#define XPRC_TYPE_INT "int"
/// string length (without null-termination) of #XPRC_TYPE_INT
#define XPRC_TYPE_INT_LENGTH (sizeof(XPRC_TYPE_INT)-1)
/// type name for a single-precision floating point value in XPRC
#define XPRC_TYPE_FLOAT "float"
/// string length (without null-termination) of #XPRC_TYPE_FLOAT
#define XPRC_TYPE_FLOAT_LENGTH (sizeof(XPRC_TYPE_FLOAT)-1)
/// type name for a double-precision floating point value in XPRC
#define XPRC_TYPE_DOUBLE "double"
/// string length (without null-termination) of #XPRC_TYPE_DOUBLE
#define XPRC_TYPE_DOUBLE_LENGTH (sizeof(XPRC_TYPE_DOUBLE)-1)
/// type name for an array of integer values in XPRC
#define XPRC_TYPE_INT_ARRAY "int[]"
/// string length (without null-termination) of #XPRC_TYPE_INT_ARRAY
#define XPRC_TYPE_INT_ARRAY_LENGTH (sizeof(XPRC_TYPE_INT_ARRAY)-1)
/// type name for an array of single-precision floating point values in XPRC
#define XPRC_TYPE_FLOAT_ARRAY "float[]"
/// string length (without null-termination) of #XPRC_TYPE_FLOAT_ARRAY
#define XPRC_TYPE_FLOAT_ARRAY_LENGTH (sizeof(XPRC_TYPE_FLOAT_ARRAY)-1)
/// type name for a byte array in XPRC
#define XPRC_TYPE_BLOB "blob"
/// string length (without null-termination) of #XPRC_TYPE_BLOB
#define XPRC_TYPE_BLOB_LENGTH (sizeof(XPRC_TYPE_BLOB)-1)

/// separator used between types in a listing of multiple types
#define XPRC_TYPE_SEPARATOR ","
/// string length (without null-termination) of #XPRC_TYPE_SEPARATOR
#define XPRC_TYPE_SEPARATOR_LENGTH 1

/// separator used between values of an array
#define XPRC_ARRAY_ITEM_SEPARATOR ","

/**
 * Parses a single type as encoded in the XPRC protocol to an X-Plane SDK data type ID.
 * @param s XPRC protocol type name
 * @param count length of XPRC type name string to parse
 * @return the matching X-Plane SDK data type ID; xplmType_Unknown if unmatched
 */
XPLMDataTypeID xprc_parse_type(char *s, int count);

/**
 * Parses a list of multiple types as encoded in the XPRC protocol to a combined X-Plane SDK data type ID.
 * @param s string containing a list of XPRC type names in protocol encoding
 * @param count length of the string to parse
 * @return all types as X-Plane SDK data type IDs combined into a single value; xplmType_Unknown on error
 */
XPLMDataTypeID xprc_parse_types(char *s, int count);

/**
 * Encodes an array to XPRC protocol representation.
 * @param type X-Plane SDK array data type ID describing the value type
 * @param arr dynamic array holding the values to encode; must use the correct item type and size; see xptypes.h
 * @return null-terminated string holding the protocol-encoded list of array values; NULL on error
 */
char* xprc_encode_array(XPLMDataTypeID type, dynamic_array_t *arr);

/**
 * Encodes a single value to XPRC protocol representation.
 * @param type X-Plane SDK data type ID describing the value type
 * @param value the value to encode; must have the correct item type and size; see xptypes.h
 * @param value_size memory size of the value; see xptypes.h
 * @return null-terminated string holding the protocol-encoded value; NULL on error
 */
char* xprc_encode_value(XPLMDataTypeID type, void *value, size_t value_size);

/**
 * Encodes one or many X-Plane SDK data type IDs to XPRC protocol representation.
 * @param types single or combined X-Plane SDK data type ID(s) to encode
 * @return null-terminated string holding the protocol-encoded list of types; NULL on error
 */
char* xprc_encode_types(XPLMDataTypeID types);

/**
 * Parses the given XPRC protocol encoded string to a single value of the specified type.
 * @param s XPRC protocol encoded string (does not need to be null-terminated; count specifies the length to parse)
 * @param count number of characters to parse from the string
 * @param type X-Plane SDK data type ID describing the value type
 * @param value where to store the parsed value; must have the correct item type and size; see xptypes.h
 * @param value_size memory size of the value; see xptypes.h
 * @return true on success, false on error
 */
bool xprc_parse_value(char *s, int count, XPLMDataTypeID type, void *value, size_t value_size);

/**
 * Parses the given XPRC protocol encoded string to a dynamic array of the specified type.
 * @param s XPRC protocol encoded string, must include length indication (does not need to be null-terminated; count specifies the length to parse)
 * @param count number of characters to parse from the string
 * @param type X-Plane SDK data type ID describing the value type
 * @return dynamic array holding all parsed values; NULL on error
 */
dynamic_array_t* xprc_parse_array(char *s, int count, XPLMDataTypeID type);

#endif
