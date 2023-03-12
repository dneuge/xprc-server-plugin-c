#include <string.h>

#include "lists.h"
#include "utils.h"

#include "protocol.h"

XPLMDataTypeID xprc_parse_type(char *s, int count) {
    if (!strncmp("int", s, count)) {
        return xplmType_Int;
    } else if (!strncmp("float", s, count)) {
        return xplmType_Float;
    } else if (!strncmp("double", s, count)) {
        return xplmType_Double;
    } else if (!strncmp("int[]", s, count)) {
        return xplmType_IntArray;
    } else if (!strncmp("float[]", s, count)) {
        return xplmType_FloatArray;
    } else if (!strncmp("blob", s, count)) {
        return xplmType_Data;
    } else {
        return xplmType_Unknown;
    }
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
    char *format = (type == xplmType_Data) ? "%ld," : "%ld";
    s = dynamic_sprintf(format, arr->length);
    if (!s || !prealloc_list_append(list, s)) {
        destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        return NULL;
    }
    total_length += strlen(s);

    // encode all values
    for (int i=0; i < arr->length; i++) {
        if (type == xplmType_IntArray) {
            s = dynamic_sprintf(",%d", dynamic_array_get_item(int32_t, arr, i));
        } else if (type == xplmType_FloatArray) {
            s = dynamic_sprintf(",%f", dynamic_array_get_item(float, arr, i));
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
    char *out = zalloc(total_length+1);
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
    if (type == xplmType_Int && value_size == 4) {
        out = dynamic_sprintf("%d", *((int32_t*) value));
    } else if (type == xplmType_Float && value_size == 4) {
        out = dynamic_sprintf("%f", *((float*) value));
    } else if (type == xplmType_Double && value_size == 8) {
        out = dynamic_sprintf("%f", *((double*) value));
    }

    return out;
}
