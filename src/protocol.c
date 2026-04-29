#include <string.h>

#include "lists.h"
#include "logger.h"
#include "utils.h"
#include "xptypes.h"

#include "protocol.h"

#if xplmType_Unknown != 0
/* type parsing relies on 0 meaning unknown */
#error "xplmType_Unknown is not 0"
#endif

XPLMDataTypeID xprc_parse_type(char *s, int count) {
    if (!s) {
        return xplmType_Unknown;
    }

    if ((count == 3) && !strncmp("int", s, count)) {
        return xplmType_Int;
    } else if ((count == 5) && !strncmp("float", s, count)) {
        return xplmType_Float;
    } else if ((count == 6) && !strncmp("double", s, count)) {
        return xplmType_Double;
    } else if ((count == 5) && !strncmp("int[]", s, count)) {
        return xplmType_IntArray;
    } else if ((count == 7) && !strncmp("float[]", s, count)) {
        return xplmType_FloatArray;
    } else if ((count == 4) && !strncmp("blob", s, count)) {
        return xplmType_Data;
    } else {
        return xplmType_Unknown;
    }
}

XPLMDataTypeID xprc_parse_types(char *s, int count) {
    XPLMDataTypeID types = xplmType_Unknown;
    XPLMDataTypeID type = xplmType_Unknown;

    while (s && count > 0) {
        int type_length = count;
        char *offset_separator = strstr(s, XPRC_TYPE_SEPARATOR);
        if (offset_separator) {
            type_length = offset_separator - s;
        }
        
        type = xprc_parse_type(s, type_length);
        if (type == xplmType_Unknown) {
            return xplmType_Unknown;
        }

        if ((types & type) != 0) {
            // duplicate or collision, type was already selected
            return xplmType_Unknown;
        }
        
        types = types | type;

        if (!offset_separator) {
            break;
        }

        int skip_count = type_length + XPRC_TYPE_SEPARATOR_LENGTH;
        s += skip_count;
        count -= skip_count;
    }

    return types;
}

char* xprc_encode_array(XPLMDataTypeID type, dynamic_array_t *arr) {
    if (!arr) {
        return NULL;
    }

    if (type != xplmType_IntArray && type != xplmType_FloatArray && type != xplmType_Data) {
        // unhandled type
        return NULL;
    }

    if (arr->length == 0) {
        // no need to go through concatenation if there are no items; only length prefix is needed
        return copy_string("0");
    }
    
    // prepare list for string concatenation
    prealloc_list_t *list = create_preallocated_list();
    if (!list) {
        return NULL;
    }

    char *s = NULL;
    int total_length = 0;

    // all encoded array types are prefixed by array length
    // int[] and float[] separate each value by comma as part of value encoding but not blob
    char *format = (type == xplmType_Data) ? "%ld" XPRC_ARRAY_ITEM_SEPARATOR : "%ld";
    s = dynamic_sprintf(format, arr->length);
    if (!s || !prealloc_list_append(list, s)) {
        destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        return NULL;
    }
    total_length += strlen(s);

    // encode all values
    for (int i=0; i < arr->length; i++) {
        if (type == xplmType_IntArray) {
            s = dynamic_sprintf(XPRC_ARRAY_ITEM_SEPARATOR "%d", dynamic_array_get_item(int32_t, arr, i));
        } else if (type == xplmType_FloatArray) {
            s = dynamic_sprintf(XPRC_ARRAY_ITEM_SEPARATOR "%f", dynamic_array_get_item(float, arr, i));
        } else {
            s = dynamic_sprintf("%02X", dynamic_array_get_item(uint8_t, arr, i));
        }
        
        if (!s || !prealloc_list_append(list, s)) {
            destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            return NULL;
        }
        
        total_length += strlen(s);
    }

    // concatenate everything to a single string
    char *out = zmalloc(total_length+1);
    if (out) {
        char *dest = out;
        prealloc_list_item_t *item = list->first_in_use_item;
        while (item) {
            int len = strlen(item->value);
            memcpy(dest, item->value, len);
            dest += len;
            item = item->next_in_use;
        }
    }
    
    destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);

    return out;
}

char* xprc_encode_value(XPLMDataTypeID type, void *value, size_t value_size) {
    if (!value || value_size < 1) {
        return NULL;
    }

    char *out = NULL;
    if (type == xplmType_Int && value_size == SIZE_XPLM_INT) {
        out = dynamic_sprintf("%d", *((int32_t*) value));
    } else if (type == xplmType_Float && value_size == SIZE_XPLM_FLOAT) {
        out = dynamic_sprintf("%f", *((float*) value));
    } else if (type == xplmType_Double && value_size == SIZE_XPLM_DOUBLE) {
        out = dynamic_sprintf("%f", *((double*) value));
    }

    return out;
}

inline static void encode_types_add(bool condition, const char *encoded, const size_t length, char **pos, bool *is_first) {
    if (!condition) {
        return;
    }
    
    if (*is_first) {
        *is_first = false;
    } else {
        strncpy(*pos, XPRC_TYPE_SEPARATOR, XPRC_TYPE_SEPARATOR_LENGTH);
        *pos = *pos + XPRC_TYPE_SEPARATOR_LENGTH;
    }
    
    strncpy(*pos, encoded, length);
    *pos = *pos + length;
}

char* xprc_encode_types(XPLMDataTypeID types) {
    bool is_int = (types & xplmType_Int) != 0;
    bool is_float = (types & xplmType_Float) != 0;
    bool is_double = (types & xplmType_Double) != 0;
    bool is_float_array = (types & xplmType_FloatArray) != 0;
    bool is_int_array = (types & xplmType_IntArray) != 0;
    bool is_blob = (types & xplmType_Data) != 0;

    int num_types = (is_int ? 1 : 0) + (is_float ? 1 : 0) + (is_double ? 1 : 0)
        + (is_float_array ? 1 : 0) + (is_int_array ? 1 : 0)
        + (is_blob ? 1 : 0);

    if (num_types <= 0) {
        return NULL;
    }

    size_t length = (is_int ? XPRC_TYPE_INT_LENGTH : 0)
        + (is_float ? XPRC_TYPE_FLOAT_LENGTH : 0)
        + (is_double ? XPRC_TYPE_DOUBLE_LENGTH : 0)
        + (is_float_array ? XPRC_TYPE_FLOAT_ARRAY_LENGTH : 0)
        + (is_int_array ? XPRC_TYPE_INT_ARRAY_LENGTH : 0)
        + (is_blob ? XPRC_TYPE_BLOB_LENGTH : 0)
        + ((num_types-1) * XPRC_TYPE_SEPARATOR_LENGTH);

    char *out = zmalloc(length + 1);
    if (!out) {
        return NULL;
    }

    char *pos = out;
    bool is_first = true;

    encode_types_add(is_int, XPRC_TYPE_INT, XPRC_TYPE_INT_LENGTH, &pos, &is_first);
    encode_types_add(is_float, XPRC_TYPE_FLOAT, XPRC_TYPE_FLOAT_LENGTH, &pos, &is_first);
    encode_types_add(is_double, XPRC_TYPE_DOUBLE, XPRC_TYPE_DOUBLE_LENGTH, &pos, &is_first);
    encode_types_add(is_int_array, XPRC_TYPE_INT_ARRAY, XPRC_TYPE_INT_ARRAY_LENGTH, &pos, &is_first);
    encode_types_add(is_float_array, XPRC_TYPE_FLOAT_ARRAY, XPRC_TYPE_FLOAT_ARRAY_LENGTH, &pos, &is_first);
    encode_types_add(is_blob, XPRC_TYPE_BLOB, XPRC_TYPE_BLOB_LENGTH, &pos, &is_first);

    out[length] = 0;
    
    return out;
}

static const XPLMDataTypeID simple_types = xplmType_Int | xplmType_Float | xplmType_Double;
static const XPLMDataTypeID array_types = xplmType_IntArray | xplmType_FloatArray | xplmType_Data;
static const XPLMDataTypeID supported_types = simple_types | array_types;

bool xprc_parse_value(char *s, int count, XPLMDataTypeID type, void *value, size_t value_size) {
    RCLOG_TRACE("xprc_parse_value s=\"%s\", count=%d, type=%d, value=%p, value_size=%zu", s, count, type, value, value_size);
        
    if (!s || !value || (count < 1)) {
        RCLOG_TRACE("xprc_parse_value pre-condition failed");
        return false;
    }

    if ((type & ~supported_types) != 0) {
        RCLOG_TRACE("xprc_parse_value unsupported type");
        return false;
    }

    if (((type == xplmType_Int) || (type == xplmType_IntArray)) && (value_size >= SIZE_XPLM_INT)) {
        char *tmp = copy_partial_string(s, count);
        if (!tmp) {
            return false;
        }
        
        *((xpint_t*)value) = atoi(tmp);
        RCLOG_TRACE("xprc_parse_value int \"%s\" => %d", tmp, *((xpint_t*)value));
        free(tmp);

        return true;
    } else if (((type == xplmType_Float) || (type == xplmType_FloatArray)) && (value_size >= SIZE_XPLM_FLOAT)) {
        char *tmp = copy_partial_string(s, count);
        if (!tmp) {
            return false;
        }
        
        *((xpfloat_t*)value) = atof(tmp);
        RCLOG_TRACE("xprc_parse_value float \"%s\" => %f", tmp, *((xpfloat_t*)value));
        free(tmp);

        return true;
    } else if ((type == xplmType_Double) && (value_size >= SIZE_XPLM_DOUBLE)) {
        char *tmp = copy_partial_string(s, count);
        if (!tmp) {
            return false;
        }
        
        *((xpdouble_t*)value) = atof(tmp);
        RCLOG_TRACE("xprc_parse_value double \"%s\" => %f", tmp, *((xpdouble_t*)value));
        free(tmp);

        return true;
    } else if ((type == xplmType_Data) && (value_size >= 1) && (count == 2)) {
        uint8_t out = 0;
        for (int i=0; i<2; i++) {
            uint8_t nibble = 0;
            char ch = s[i];
            if ((ch >= '0') && (ch <= '9')) {
                nibble = (ch - '0');
            } else if ((ch >= 'A') && (ch <= 'F')) {
                nibble = 10 + (ch - 'A');
            } else if ((ch >= 'a') && (ch <= 'f')) {
                nibble = 10 + (ch - 'a');
            } else {
                return false;
            }
            
            if (i == 0) {
                out = nibble;
            } else {
                out = out << 4;
                out += nibble;
            }
        }

        *((uint8_t*)value) = out;
        RCLOG_TRACE("xprc_parse_value byte %c%c => %02X", s[0], s[1], out);
        
        return true;
    }
    
    RCLOG_TRACE("xprc_parse_value unhandled");
 
    return false;
}

dynamic_array_t* xprc_parse_array(char *s, int count, XPLMDataTypeID type) {
    RCLOG_TRACE("xprc_parse_array s=\"%s\", count=%d, type=%d", s, count, type);
    
    if (!s || (count < 1)) {
        RCLOG_TRACE("xprc_parse_array pre-condition failed");
        return NULL;
    }

    if ((type & ~array_types) != 0) {
        RCLOG_TRACE("xprc_parse_array unsupported type");
        return NULL;
    }

    size_t item_size = (type == xplmType_Data) ? 1 : SIZE_XPLM_INT_FLOAT;
    int num_separators = count_chars(s, XPRC_ARRAY_ITEM_SEPARATOR[0], count);
    RCLOG_TRACE("xprc_parse_array item_size=%zu, num_separators=%d", item_size, num_separators);
    if (num_separators < 0) {
        return NULL;
    }

    int offset_separator = strpos(s, XPRC_ARRAY_ITEM_SEPARATOR, 0);
    RCLOG_TRACE("xprc_parse_array offset_separator=%d", offset_separator);
    if ((offset_separator == 0) || (offset_separator > count)) {
        return NULL;
    }

    int arr_length = 0;
    int item_length = (offset_separator > 0) ? offset_separator : count;
    RCLOG_TRACE("xprc_parse_array item_length=%d", item_length);
    char *tmp = copy_partial_string(s, item_length);
    if (!tmp) {
        return NULL;
    }

    arr_length = atoi(tmp);
    RCLOG_TRACE("xprc_parse_array arr_length=%d parsed from \"%s\"", arr_length, tmp);
    free(tmp);

    if (type == xplmType_Data) {
        int blob_strlen = count - offset_separator - 1;
        int blob_length = blob_strlen / 2;
        if ((blob_length != arr_length) || (blob_strlen % 2)) {
            RCLOG_TRACE("xprc_parse_array bad array length for blob: strlen=%d, len=%d, rem=%d", blob_strlen, blob_length, (blob_strlen % 2));
            return NULL;
        }
    } else if ((arr_length < 0) || ((arr_length != 0) && (arr_length != num_separators)) || ((arr_length == 0) && ((num_separators > 1) || (count > 2)))) {
        RCLOG_TRACE("xprc_parse_array bad array length for non-blob");
        return NULL;
    }

    dynamic_array_t *arr = create_dynamic_array(item_size, arr_length);
    if (!arr) {
        RCLOG_WARN("xprc_parse_array array allocation failed");
        return NULL;
    }
    if (!dynamic_array_set_length(arr, arr_length)) {
        RCLOG_WARN("xprc_parse_array failed to set array length");
        destroy_dynamic_array(arr);
        return NULL;
    }

    if (arr_length == 0) {
        RCLOG_TRACE("xprc_parse_array succeeded with empty array");
        return arr;
    }

    int i = 0;
    int next_offset_separator = 0;
    if (type == xplmType_Data) {
        item_length = 2;
    }
    while ((offset_separator + 1 < count) && (next_offset_separator >= 0)) {
        if (type == xplmType_Data) {
            next_offset_separator = offset_separator + 2;
            if (next_offset_separator + 1 >= count) {
                next_offset_separator = -1;
            }
        } else {
            next_offset_separator = strpos(s, XPRC_ARRAY_ITEM_SEPARATOR, offset_separator + 1);
            item_length = ((next_offset_separator > offset_separator) ? next_offset_separator : count) - offset_separator - 1;
            if (item_length < 1) {
                RCLOG_TRACE("xprc_parse_array bad item_length: offset_separator=%d, next_offset_separator=%d, item_length=%d", offset_separator, next_offset_separator, item_length);
                destroy_dynamic_array(arr);
                return NULL;
            }
        }
        
        RCLOG_TRACE("xprc_parse_array offset_separator=%d, next_offset_separator=%d, item_length=%d", offset_separator, next_offset_separator, item_length);
        
        void *value = dynamic_array_get_pointer(arr, i++);
        if (!value || !xprc_parse_value(&(s[offset_separator + 1]), item_length, type, value, arr->item_size)) {
            RCLOG_TRACE("xprc_parse_array failed to parse");
            destroy_dynamic_array(arr);
            return NULL;
        }

        offset_separator = next_offset_separator;
    }
    
    if (i != arr_length) {
        RCLOG_TRACE("xprc_parse_array bad total length: i=%d, expected to match arr_length=%d", i, arr_length);
        destroy_dynamic_array(arr);
        return NULL;
    }
    
    RCLOG_TRACE("xprc_parse_array succeeded");
    
    return arr;
}
