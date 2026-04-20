#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "hashmap.h"
#include "lists.h"
#include "logger.h"
#include "utils.h"

#include "license_manager.h"

#define ACCEPTED_LICENSES_FILENAME "xprc_accepted_licenses.cfg"
#define LICENSE_ID_SEPARATOR "\t"

static pending_license_t* create_pending_license(char *license_id, xprc_license_hash_t *previously_accepted_hash) {
    if (!license_id) {
        RCLOG_ERROR("create_pending_license called with null license ID");
        return NULL;
    }

    pending_license_t *out = zmalloc(sizeof(pending_license_t));
    if (!out) {
        return NULL;
    }

    out->license_id = license_id;
    out->previously_accepted = (previously_accepted_hash != NULL);
    out->accepted_hash = previously_accepted_hash ? *previously_accepted_hash : 0;

    return out;
}

static pending_license_t* copy_pending_license(pending_license_t *original) {
    return copy_memory(original, sizeof(pending_license_t));
}

void destroy_pending_license(void *ref) {
    pending_license_t *pending_license = ref;
    if (!pending_license) {
        return;
    }

    pending_license->license_id = NULL;  // pointer to shared memory, must not be freed
    pending_license->previously_accepted = false;
    pending_license->accepted_hash = 0;

    free(pending_license);
}

static void free_hashmap_value(char *key, void *value) {
    free(value);
}

static hashmap_t* load_accepted_licenses(char *path) {
    hashmap_t *out = create_hashmap();
    if (!out) {
        return NULL;
    }

    list_t *lines = create_list();
    if (!lines) {
        RCLOG_ERROR("failed to create list for reading accepted licenses");
        return out;
    }

    error_t err = read_lines_from_file(&lines, path);
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to read accepted licenses from %s", path);
        goto error;
    }

    for (list_item_t *item = lines->head; item; item = item->next) {
        char *line = item->value;
        int separator_offset = strpos(line, LICENSE_ID_SEPARATOR, 0);
        if (separator_offset < 1) {
            RCLOG_WARN("ID separator not found, skipping bad line in %s: \"%s\"", path, line);
            continue;
        }

        xprc_license_hash_t *accepted_license_hash = zmalloc(sizeof(xprc_license_hash_t));
        if (!accepted_license_hash) {
            RCLOG_ERROR("failed to allocate hash");
            goto error;
        }

        line[separator_offset] = 0;
        char *accepted_license_id = line;
        char *accepted_license_hash_str = &line[separator_offset+1];
        if (!xprc_parse_license_hash(accepted_license_hash, accepted_license_hash_str)) {
            RCLOG_WARN("hash did not parse, skipping bad line in %s: \"%s\"", path, line);
            free(accepted_license_hash);
            continue;
        }

        xprc_license_t *license = xprc_get_license(accepted_license_id);
        if (!license) {
            RCLOG_WARN("previously accepted license \"%s\" no longer exists, skipping", accepted_license_id);
            free(accepted_license_hash);
            continue;
        }

        xprc_license_t *old_hash = NULL;
        if (!hashmap_put(out, license->id, accepted_license_hash, (void**)&old_hash)) {
            RCLOG_WARN("failed to record accepted hash for license \"%s\" while loading", license->id);
            goto error;
        }
        if (old_hash) {
            RCLOG_WARN("multiple hashes have been persisted for license \"%s\"", license->id);
            free(old_hash);
        }
    }

    goto end;

error:
    // on error we still want to return an empty map, if possible
    if (!is_hashmap_empty(out)) {
        destroy_hashmap(out, free_hashmap_value);
        out = create_hashmap();
    }

end:
    destroy_list(lines, free);

    return out;
}

static list_t* compare_licenses(hashmap_t *accepted_licenses) {
    if (!accepted_licenses) {
        RCLOG_ERROR("compare_licenses: called with NULL");
        return NULL;
    }

    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    list_t *license_ids = xprc_get_license_ids();
    if (!license_ids) {
        RCLOG_ERROR("compare_licenses: failed to get license IDs");
        goto error;
    }

    for (list_item_t *item = license_ids->head; item; item = item->next) {
        char *license_id = item->value;

        xprc_license_t *license = xprc_get_license(license_id);
        if (!license) {
            RCLOG_ERROR("compare_licenses: unable to retrieve license \"%s\"", license_id);
            goto error;
        }

        xprc_license_hash_t *accepted_license_hash = hashmap_get(accepted_licenses, license_id);
        if (accepted_license_hash && license->hash == *accepted_license_hash) {
            RCLOG_DEBUG("compare_licenses: \"%s\" already accepted", license->id);
            continue;
        }

        RCLOG_DEBUG("compare_licenses: \"%s\" is new or has changed", license->id);
        pending_license_t *pending_license = create_pending_license(license->id, accepted_license_hash);
        if (!pending_license) {
            RCLOG_ERROR("compare_licenses: failed to create pending license");
            goto error;
        }

        if (!list_append(out, pending_license)) {
            RCLOG_ERROR("compare_licenses: failed to record pending license");
            destroy_pending_license(pending_license);
            goto error;
        }
    }

    goto end;

error:
    destroy_list(out, destroy_pending_license);
    out = NULL;

end:
    destroy_list(license_ids, NULL);

    return out;
}

license_manager_t* create_license_manager(char *directory, license_manager_callback_f on_acceptance, void *on_acceptance_ref, license_manager_callback_f on_rejection, void *on_rejection_ref) {
    hashmap_t *accepted_licenses = NULL;

    if (!on_acceptance || !on_rejection) {
        RCLOG_ERROR("create_license_manager: missing arguments, on_acceptance=%p, on_rejection=%p", on_acceptance, on_rejection);
        return NULL;
    }

    license_manager_t *out = zmalloc(sizeof(license_manager_t));
    if (!out) {
        return NULL;
    }

    out->on_acceptance = on_acceptance;
    out->on_acceptance_ref = on_acceptance_ref;

    out->on_rejection = on_rejection;
    out->on_rejection_ref = on_rejection_ref;

    out->license_acceptance_file_path = dynamic_sprintf("%s%c%s", directory, DIRECTORY_SEPARATOR, ACCEPTED_LICENSES_FILENAME);
    if (!out->license_acceptance_file_path) {
        RCLOG_ERROR("failed to construct license acceptance path");
        goto error;
    }

    out->_file_found = check_file_exists(out->license_acceptance_file_path);

    accepted_licenses = load_accepted_licenses(out->license_acceptance_file_path);
    if (!accepted_licenses) {
        // NOTE: If loading fails, we would still get an empty map as fallback.
        //       This case only happens if we couldn't even do that.
        RCLOG_ERROR("failed to initialize accepted licenses");
        goto error;
    }

    out->_pending_licenses = compare_licenses(accepted_licenses);
    if (!out->_pending_licenses) {
        RCLOG_ERROR("failed to compare licenses");
        goto error;
    }

    goto end;

error:
    destroy_license_manager(out);
    out = NULL;

end:
    if (accepted_licenses) {
        destroy_hashmap(accepted_licenses, free_hashmap_value);
    }

    return out;
}

void perform_initial_license_check(license_manager_t *license_manager) {
    if (!license_manager) {
        return;
    }

    if (!all_licenses_accepted(license_manager)) {
        RCLOG_WARN("perform_initial_license_check: licenses have not been accepted yet, deferring startup");
    } else {
        RCLOG_DEBUG("perform_initial_license_check: licenses have already been accepted, triggering acceptance callback");
        if (license_manager->on_acceptance) {
            license_manager->on_acceptance(license_manager->on_acceptance_ref);
        }
    }
}

void destroy_license_manager(license_manager_t *license_manager) {
    if (!license_manager) {
        return;
    }

    if (license_manager->license_acceptance_file_path) {
        free(license_manager->license_acceptance_file_path);
        license_manager->license_acceptance_file_path = NULL;
    }

    license_manager->on_acceptance = NULL;
    license_manager->on_rejection = NULL;

    destroy_list(license_manager->_pending_licenses, destroy_pending_license);
    license_manager->_pending_licenses = NULL;
}

list_t* get_pending_licenses(license_manager_t *license_manager) {
    if (!license_manager) {
        RCLOG_ERROR("get_pending_licenses called with NULL");
        return NULL;
    }

    if (!license_manager->_pending_licenses) {
        RCLOG_ERROR("get_pending_licenses: list of pending licenses is unavailable");
        return NULL;
    }

    list_t *out = create_list();
    if (!out) {
        RCLOG_WARN("get_pending_licenses: failed to create list");
        return NULL;
    }

    for (list_item_t *item = license_manager->_pending_licenses->head; item; item = item->next) {
        pending_license_t *original = item->value;
        pending_license_t *copy = copy_pending_license(original);
        if (!copy) {
            RCLOG_WARN("get_pending_licenses: failed to copy %p", original);
            goto error;
        }

        if (!list_append(out, copy)) {
            RCLOG_WARN("get_pending_licenses: failed to append %p", copy);
            destroy_pending_license(copy);
            goto error;
        }
    }

    return out;

error:
    destroy_list(out, destroy_pending_license);

    return NULL;
}

static bool test_pending_license_id(void *value, void *ref) {
    pending_license_t *pending_license = value;
    char *wanted_license_id = ref;

    if (!wanted_license_id || !pending_license || !pending_license->license_id) {
        return false;
    }

    return strcmp(pending_license->license_id, wanted_license_id) == 0;
}

error_t get_pending_license(pending_license_t **pending_license, license_manager_t *license_manager, char *license_id) {
    if (!pending_license || !license_manager || !license_id) {
        RCLOG_WARN("get_pending_license: missing parameters, pending_license=%p, license_manager=%p, license_id=%p", pending_license, license_manager, license_id);
        return ERROR_UNSPECIFIC;
    }

    if (!license_manager->_pending_licenses) {
        RCLOG_ERROR("get_pending_license: license manager at %p is missing pending license list", license_manager);
        return ERROR_UNSPECIFIC;
    }

    if (!xprc_get_license(license_id)) {
        RCLOG_WARN("get_pending_license called for unknown license %s", license_id);
        return ERROR_UNSPECIFIC;
    }

    pending_license_t *res = NULL;
    list_item_t *item = list_find_test(license_manager->_pending_licenses, test_pending_license_id, license_id);
    if (item) {
        res = copy_pending_license(item->value);
        if (!res) {
            return ERROR_MEMORY_ALLOCATION;
        }
    }
    *pending_license = res;

    return ERROR_NONE;
}

bool all_licenses_accepted(license_manager_t *license_manager) {
    if (!license_manager) {
        RCLOG_ERROR("all_licenses_accepted: called with NULL");
        return false;
    }

    if (!license_manager->_pending_licenses) {
        RCLOG_ERROR("all_licenses_accepted: list of pending licenses is missing");
        return false;
    }

    return (license_manager->_pending_licenses->head == NULL);
}

bool no_licenses_accepted(license_manager_t *license_manager) {
    if (!license_manager) {
        RCLOG_ERROR("no_licenses_accepted: called with NULL");
        return false;
    }

    // since we delete the license acceptance file when licenses are being rejected
    // and that file does not exist initially, the answer is really equivalent to mere
    // presence/absence of that file
    return !license_manager->_file_found;
}

static void persist_all_licenses(char *path) {
    if (!path) {
        RCLOG_ERROR("persist_all_licenses: called with NULL");
        return;
    }

    list_t *lines = create_list();
    if (!lines) {
        return;
    }

    list_t *license_ids = xprc_get_license_ids();
    if (!license_ids) {
        RCLOG_WARN("persist_all_licenses: failed to get license IDs");
        goto error;
    }

    for (list_item_t *item = license_ids->head; item; item = item->next) {
        char *license_id = item->value;

        xprc_license_t *license = xprc_get_license(license_id);
        if (!license) {
            RCLOG_ERROR("persist_all_licenses: failed to retrieve license \"%s\"", license_id);
            goto error;
        }

        char *license_hash_formatted = xprc_format_license_hash(license->hash);
        if (!license_hash_formatted) {
            RCLOG_ERROR("persist_all_licenses: failed to format license hash for \"%s\"", license->id);
            goto error;
        }

        char *line = dynamic_sprintf("%s%s%s", license->id, LICENSE_ID_SEPARATOR, license_hash_formatted);
        free(license_hash_formatted);
        license_hash_formatted = NULL;
        if (!line) {
            RCLOG_WARN("persist_all_licenses: failed to format line \"%s\"", license->id);
            goto error;
        }

        if (!list_append(lines, line)) {
            RCLOG_WARN("persist_all_licenses: failed to append line");
            free(line);
            goto error;
        }
    }
    
    RCLOG_DEBUG("persist_all_licenses: writing to %s", path);
    error_t err = write_lines_to_file(lines, path);
    if (err != ERROR_NONE) {
        RCLOG_WARN("persist_all_licenses: failed writing to %s (%d)", path, err);
        goto error;
    }

    goto end;
    
error:
end:
    destroy_list(license_ids, NULL);
    destroy_list(lines, free);
}

error_t accept_all_licenses(license_manager_t *license_manager) {
    if (!license_manager) {
        return ERROR_UNSPECIFIC;
    }

    persist_all_licenses(license_manager->license_acceptance_file_path);

    destroy_list(license_manager->_pending_licenses, destroy_pending_license);
    license_manager->_pending_licenses = create_list();
    if (!license_manager->_pending_licenses) {
        RCLOG_ERROR("accept_all_licenses: failed to create empty list of pending licenses");
    }

    RCLOG_DEBUG("accept_all_licenses: triggering acceptance callback");
    if (license_manager->on_acceptance) {
        license_manager->on_acceptance(license_manager->on_acceptance_ref);
    }

    return ERROR_NONE;
}

static list_t* get_all_licenses_as_pending() {
    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    list_t *license_ids = xprc_get_license_ids();
    if (!license_ids) {
        RCLOG_WARN("get_all_licenses_as_pending: failed to get license IDs");
        goto error;
    }

    for (list_item_t *item = license_ids->head; item; item = item->next) {
        char *license_id = item->value;

        pending_license_t *pending_license = create_pending_license(license_id, NULL);
        if (!pending_license) {
            RCLOG_WARN("get_all_licenses_as_pending: failed to create pending license");
            goto error;
        }

        if (!list_append(out, pending_license)) {
            RCLOG_WARN("get_all_licenses_as_pending: failed to append pending license");
            free(pending_license);
            goto error;
        }
    }

    goto end;

error:
    destroy_list(out, destroy_pending_license);
    out = NULL;

end:
    destroy_list(license_ids, NULL);

    return out;
}

error_t reject_licenses(license_manager_t *license_manager) {
    if (!license_manager) {
        return ERROR_UNSPECIFIC;
    }

    if (!check_file_exists(license_manager->license_acceptance_file_path)) {
        RCLOG_DEBUG("reject_licenses: file does not exist: %s", license_manager->license_acceptance_file_path);
    } else {
        RCLOG_DEBUG("reject_licenses: attempting to delete %s", license_manager->license_acceptance_file_path);
        int res = remove(license_manager->license_acceptance_file_path);
        if (res != 0) {
            RCLOG_WARN("reject_licenses: failed to delete %s (%d)", license_manager->license_acceptance_file_path, res);
        }
    }

    list_t *new_pending = get_all_licenses_as_pending();
    if (!new_pending) {
        RCLOG_WARN("reject_licenses: failed to update pending licenses");
    } else {
        list_t *old_pending = license_manager->_pending_licenses;
        license_manager->_pending_licenses = new_pending;
        destroy_list(old_pending, destroy_pending_license);
    }

    RCLOG_DEBUG("reject_licenses: triggering rejection callback");
    if (license_manager->on_rejection) {
        license_manager->on_rejection(license_manager->on_rejection_ref);
    }

    return ERROR_NONE;
}
