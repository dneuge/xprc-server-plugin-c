#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "fileio.h"
#include "logger.h"
#include "network.h"
#include "password.h"
#include "utils.h"

#include "settings.h"

#define XPRC_DEFAULT_NETWORK_ENABLE_IPV6 false
#define XPRC_DEFAULT_NETWORK_PORT 23042
#if (XPRC_DEFAULT_NETWORK_PORT < NETWORK_MINIMUM_PORT) || (XPRC_DEFAULT_NETWORK_PORT > NETWORK_MAXIMUM_PORT)
#error "Default port exceeds valid network port range!"
#endif

/// XPRC default settings
static const settings_t default_settings = {
        .password = NULL, // must be auto-generated
        .auto_startup = true,
        .auto_regen_password = true,
        .network_interface = XPRC_DEFAULT_NETWORK_INTERFACE,
        .network_port = XPRC_DEFAULT_NETWORK_PORT,
        .network_enable_ipv6 = XPRC_DEFAULT_NETWORK_ENABLE_IPV6
};

#define SETTINGS_FIELD_TYPE_END_OF_FIELDS 0
#define SETTINGS_FIELD_TYPE_STRING 1
#define SETTINGS_FIELD_TYPE_BOOLEAN 2
#define SETTINGS_FIELD_TYPE_INTEGER 3
typedef uint8_t settings_field_type_t;

#define SETTINGS_SERIALIZATION_KEY_VALUE_SEPARATOR "="

#define SETTINGS_SERIALIZATION_TRUE "true"
#define SETTINGS_SERIALIZATION_FALSE "false"
#define SETTINGS_SERIALIZATION_NULL "#!<>$NULL$<>#!"

typedef struct {
    char *key;
    settings_field_type_t type;
    size_t offset;
} settings_field_t;

static const settings_field_t settings_fields[] = {
    {
        .key = "auto_startup",
        .type = SETTINGS_FIELD_TYPE_BOOLEAN,
        .offset = offsetof(settings_t, auto_startup)
    },

    {
        .key = "auto_regen_password",
        .type = SETTINGS_FIELD_TYPE_BOOLEAN,
        .offset = offsetof(settings_t, auto_regen_password)
    },

    {
        .key = "network_interface",
        .type = SETTINGS_FIELD_TYPE_STRING,
        .offset = offsetof(settings_t, network_interface)
    },

    {
        .key = "network_port",
        .type = SETTINGS_FIELD_TYPE_INTEGER,
        .offset = offsetof(settings_t, network_port)
    },

    {
        .key = "network_enable_ipv6",
        .type = SETTINGS_FIELD_TYPE_BOOLEAN,
        .offset = offsetof(settings_t, network_enable_ipv6)
    },

    {
        .key = NULL,
        .type = SETTINGS_FIELD_TYPE_END_OF_FIELDS,
        .offset = 0
    }
};

static char* serialize_setting(settings_t *settings, settings_field_t *field) {
    if (!field) {
        RCLOG_WARN("[settings] tried to serialize using NULL field descriptor");
        return NULL;
    }

    void *value_ref = (void*)settings + field->offset; // void* cast needed, else access gets multiplied by sizeof(settings_t)

    if (!field->key) {
        RCLOG_WARN("[settings] tried to serialize field without key");
        return NULL;
    }

    switch (field->type) {
        case SETTINGS_FIELD_TYPE_BOOLEAN:
            RCLOG_TRACE("[settings] serializing boolean from %p", value_ref);
            bool bool_value = *((bool*) value_ref);
            RCLOG_TRACE("[settings] ... boolean value: %d", bool_value);
            return dynamic_sprintf(
                "%s%s%s",
                field->key,
                SETTINGS_SERIALIZATION_KEY_VALUE_SEPARATOR,
                bool_value ? SETTINGS_SERIALIZATION_TRUE : SETTINGS_SERIALIZATION_FALSE
            );

        case SETTINGS_FIELD_TYPE_INTEGER:
            RCLOG_TRACE("[settings] serializing int from %p: %d", value_ref, *((int*) value_ref));
            int int_value = *((int*) value_ref);
            RCLOG_TRACE("[settings] ... integer value: %d", int_value);
            return dynamic_sprintf(
                "%s%s%d",
                field->key,
                SETTINGS_SERIALIZATION_KEY_VALUE_SEPARATOR,
                int_value
            );

        case SETTINGS_FIELD_TYPE_STRING:
            RCLOG_TRACE("[settings] serializing char* from %p", value_ref);
            RCLOG_TRACE("[settings] ... pointer to: %p", *(void**)value_ref);
            char *string_value = *((char**) value_ref);
            RCLOG_TRACE("[settings] ... string value: %s", string_value);
            return dynamic_sprintf(
                "%s%s%s",
                field->key,
                SETTINGS_SERIALIZATION_KEY_VALUE_SEPARATOR,
                string_value ? string_value : SETTINGS_SERIALIZATION_NULL
            );

        default:
            RCLOG_WARN("[settings] unable to serialize \"%s\", type %u is not handled", field->key, field->type);
            return NULL;
    }
}

static list_t* serialize_settings(settings_t *settings) {
    list_t *lines = create_list();
    if (!lines) {
        RCLOG_WARN("[settings] failed to create list for serialization");
        return NULL;
    }

    RCLOG_TRACE("[settings] serializing fields from %p (size %lu)", settings_fields, sizeof(settings_field_t));
    settings_field_t *field = (settings_field_t*) &settings_fields;
    while (field->type != SETTINGS_FIELD_TYPE_END_OF_FIELDS) {
        RCLOG_TRACE("[settings] serialization processes field: field=%p (type=%d, key=%s, offset=%lu)", field, field->type, field->key, field->offset);

        char *line = serialize_setting(settings, field);
        if (!line) {
            RCLOG_WARN("[settings] failed to serialize field");
            goto error;
        }
        RCLOG_TRACE("[settings] serialized field: %s", line);

        if (!list_append(lines, line)) {
            RCLOG_WARN("[settings] failed to append serialized line");
            free(line);
            goto error;
        }

        field++; // this already increments by sizeof(settings_field_t) due to type
    }

    return lines;

error:
    destroy_list(lines, free);
    return NULL;
}

static settings_field_t* get_settings_field(char *key) {
    settings_field_t *field = (settings_field_t*) &settings_fields;
    while (field->type != SETTINGS_FIELD_TYPE_END_OF_FIELDS) {
        if (!strcmp(key, field->key)) {
            return field;
        }

        field++; // this already increments by sizeof(settings_field_t) due to type
    }

    return NULL;
}

static error_t deserialize_setting(settings_t *settings, settings_field_t *field, char *s) {
    void *value_ref = (void*)settings + field->offset; // void* cast needed, else access gets multiplied by sizeof(settings_t)

    RCLOG_TRACE("[settings] deserializing field key=%s, type=%d, s=\"%s\"", field->key, field->type, s);
    switch (field->type) {
        case SETTINGS_FIELD_TYPE_BOOLEAN:
            if (!strcmp(s, SETTINGS_SERIALIZATION_TRUE)) {
                RCLOG_TRACE("[settings] ... boolean is true");
                *((bool*) value_ref) = true;
            } else if (!strcmp(s, SETTINGS_SERIALIZATION_FALSE)) {
                RCLOG_TRACE("[settings] ... boolean is false");
                *((bool*) value_ref) = false;
            } else {
                RCLOG_WARN("[settings] field \"%s\" holds no supported value for a boolean: \"%s\"", field->key, s);
                return ERROR_UNSPECIFIC;
            }
            return ERROR_NONE;

        case SETTINGS_FIELD_TYPE_INTEGER:
            int int_value = 0;
            error_t err = parse_int(&int_value, s);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[settings] field \"%s\" holds invalid value for an integer: \"%s\" (error %d)", field->key, s, err);
                return ERROR_UNSPECIFIC;
            }
            RCLOG_TRACE("[settings] ... integer value is %d", int_value);
            *(int*)value_ref = int_value;
            return ERROR_NONE;

        case SETTINGS_FIELD_TYPE_STRING:
            char *old_value_s = *((char**) value_ref);

            char *new_value_s = NULL;
            if (strcmp(s, SETTINGS_SERIALIZATION_NULL) != 0) {
                // string does NOT indicate null in this case
                // in case it should indicate null (else) we keep new_value_s at NULL
                new_value_s = copy_string(s);
                if (!new_value_s) {
                    RCLOG_WARN("[settings] failed to copy string for deserialization");
                    return ERROR_MEMORY_ALLOCATION;
                }
            }

            if (old_value_s) {
                free(old_value_s);
            }

            RCLOG_TRACE("[settings] ... string value is \"%s\"", new_value_s);
            *((char**) value_ref) = new_value_s;

            return ERROR_NONE;

        default:
            RCLOG_WARN("[settings] field \"%s\" has unhandled type %u, deserialization is not supported", field->key, field->type);
            return ERROR_UNSPECIFIC;
    }
}

static error_t deserialize_settings(settings_t *settings, list_t *lines) {
    error_t err = ERROR_NONE;

    list_item_t *line_item = lines->head;
    while (line_item) {
        char *line = line_item->value;

        int separator = strpos(line, SETTINGS_SERIALIZATION_KEY_VALUE_SEPARATOR, 0);
        if (separator < 1) {
            return ERROR_UNSPECIFIC;
        }

        char *key_copy = copy_partial_string(line, separator);
        char *value = line + separator + 1;

        settings_field_t *field = get_settings_field(key_copy);
        free(key_copy);
        key_copy = NULL;

        // Only deserialize known fields; unknown fields loaded from file can be skipped, same as known fields that
        // are missing from the file - this will eventually happen as the plugin gets updated and users switch between
        // versions (both up and down). We want to load what we can and use defaults for the rest.
        if (field) {
            err = deserialize_setting(settings, field, value);
            if (err != ERROR_NONE) {
                break;
            }
        }

        line_item = line_item->next;
    }

    return err;
}

settings_t* create_settings() {
    RCLOG_TRACE("[settings] creating new instance");

    settings_t *out = zalloc(sizeof(settings_t));
    if (!out) {
        RCLOG_WARN("[settings] failed to create settings (out of memory?)");
        return NULL;
    }

    RCLOG_TRACE("[settings] copying default settings for new instance");
    error_t err = copy_settings(out, (settings_t*) &default_settings, SETTINGS_KEEP_PASSWORD);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings] failed to copy default settings to new instance; error %d", err);
        destroy_settings(out);
        return NULL;
    }

    RCLOG_TRACE("[settings] generating password for new instance");
    out->password = generate_password();
    if (!out->password) {
        RCLOG_WARN("[settings] failed to generate password for new instance");
        destroy_settings(out);
        return NULL;
    }

    return out;
}

void destroy_settings(settings_t *settings) {
    if (!settings) {
        return;
    }

    if (settings->password) {
        free(settings->password);
        settings->password = NULL;
    }

    if (settings->network_interface) {
        free(settings->network_interface);
        settings->network_interface = NULL;
    }

    free(settings);
}

error_t copy_settings(settings_t *dest, settings_t *src, bool copy_password) {
    if (!dest || !src || ((copy_password == SETTINGS_COPY_PASSWORD) && !src->password)) {
        RCLOG_WARN("[settings] missing input to copy_settings: dest=%p, src=%p, copy_password=%d, src->password=%p", dest, src, copy_password, src ? src->password : NULL);
        return ERROR_UNSPECIFIC;
    }

    char *network_interface = copy_string(src->network_interface);
    if (src->network_interface && !network_interface) {
        RCLOG_WARN("[settings] failed to copy network interface");
        return ERROR_MEMORY_ALLOCATION;
    }

    char *password = NULL;
    if (copy_password == SETTINGS_COPY_PASSWORD) {
        password = copy_string(src->password);
        if (!password) {
            RCLOG_WARN("[settings] failed to copy password");
            if (network_interface) {
                free(network_interface);
            }
            return ERROR_MEMORY_ALLOCATION;
        }

        if (dest->password) {
            free(dest->password);
        }
        dest->password = password;
    }

    if (dest->network_interface) {
        free(dest->network_interface);
    }
    dest->network_interface = network_interface;

    dest->network_enable_ipv6 = src->network_enable_ipv6;
    dest->network_port = src->network_port;
    dest->auto_regen_password = src->auto_regen_password;
    dest->auto_startup = src->auto_startup;

    return ERROR_NONE;
}

error_t load_settings_without_password(settings_t *dest, char *filepath) {
    /* Settings are first deserialized onto a temporary instance as loading may be aborted mid-way in case of
     * deserialization errors in which case the destination settings should remain unmodified but the settings
     * used for deserialization have already been modified.
     */

    error_t err = ERROR_NONE;

    if (!dest || !filepath) {
        RCLOG_WARN("[settings] missing input for load_settings_without_password: dest=%p, filepath=%s", dest, filepath);
        return ERROR_UNSPECIFIC;
    }

    settings_t *settings = create_settings();
    if (!settings) {
        RCLOG_WARN("[settings] failed to create settings for loading");
        return ERROR_MEMORY_ALLOCATION;
    }

    RCLOG_DEBUG("[settings] loading settings from %s", filepath);
    list_t *lines = NULL;
    err = read_lines_from_file(&lines, filepath);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings] failed to read lines from %s", filepath);
        goto end;
    }

    err = deserialize_settings(settings, lines);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings] deserialization failed; settings will remain unchanged");
        goto end;
    }

    RCLOG_DEBUG("[settings] storing loaded settings");
    err = copy_settings(dest, settings, SETTINGS_KEEP_PASSWORD);

end:
    destroy_settings(settings);
    settings = NULL;

    return err;
}

error_t save_settings_without_password(settings_t *settings, char *filepath) {
    list_t *lines = NULL;
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!settings || !filepath) {
        return ERROR_UNSPECIFIC;
    }

    lines = serialize_settings(settings);
    if (!lines) {
        RCLOG_WARN("[settings] serialization failed");
        out_err = ERROR_UNSPECIFIC;
    } else {
        err = write_lines_to_file(lines, filepath);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings] could not write to file %s", filepath);
            out_err = err;
        }
    }

    if (lines) {
        destroy_list(lines, free);
        lines = NULL;
    }

    return out_err;
}

error_t load_password(settings_t *dest, char *filepath) {
    error_t out_err = ERROR_NONE;

    if (!dest || !filepath) {
        return ERROR_UNSPECIFIC;
    }

    list_t *lines = NULL;
    error_t err = read_lines_from_file(&lines, filepath);
    if (err != ERROR_NONE) {
        return err;
    }

    if (!lines || lines->size < 1) {
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    char *password_copy = copy_string(lines->head->value);
    if (!password_copy) {
        out_err = ERROR_MEMORY_ALLOCATION;
        goto end;
    }

    if (dest->password) {
        free(dest->password);
    }
    dest->password = password_copy;

end:
    if (lines) {
        destroy_list(lines, free);
        lines = NULL;
    }

    return out_err;
}

error_t save_password(settings_t *settings, char *filepath) {
    if (!settings || !filepath || !settings->password) {
        return ERROR_UNSPECIFIC;
    }

    return write_file(settings->password, strlen(settings->password), filepath);
}

bool validate_settings(settings_t *settings, bool check_password) {
    if (!settings) {
        return false;
    }

    if (check_password && !validate_password(settings->password)) {
        return false;
    }

    if ((settings->network_port < NETWORK_MINIMUM_PORT) || (settings->network_port > NETWORK_MAXIMUM_PORT)) {
        return false;
    }

    // FIXME: check network interface for existence

    return true;
}

bool constrain_settings(settings_t *settings) {
    if (!settings) {
        return false;
    }

    if (settings->network_port < NETWORK_MINIMUM_PORT) {
        settings->network_port = NETWORK_MINIMUM_PORT;
        return true;
    }

    if (settings->network_port > NETWORK_MAXIMUM_PORT) {
        settings->network_port = NETWORK_MAXIMUM_PORT;
        return true;
    }

    return false;
}

error_t reset_network_settings(settings_t *settings) {
    if (!settings) {
        return ERROR_UNSPECIFIC;
    }

    char *network_interface_new = copy_string(XPRC_DEFAULT_NETWORK_INTERFACE);
    if (XPRC_DEFAULT_NETWORK_INTERFACE && !network_interface_new) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (settings->network_interface) {
        free(settings->network_interface);
    }
    settings->network_interface = network_interface_new;

    settings->network_port = XPRC_DEFAULT_NETWORK_PORT;
    settings->network_enable_ipv6 = XPRC_DEFAULT_NETWORK_ENABLE_IPV6;

    return ERROR_NONE;
}