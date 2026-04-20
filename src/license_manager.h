#ifndef XPRC_LICENSE_MANAGER_H
#define XPRC_LICENSE_MANAGER_H

#include "errors.h"
#include "lists.h"
#include "licenses.h"

typedef void (*license_manager_callback_f)(void *ref);

typedef struct {
    char *license_id;  // points to shared memory, must not be modified/freed
    bool previously_accepted;
    xprc_license_hash_t accepted_hash;
} pending_license_t;

typedef struct {
    char *license_acceptance_file_path;

    license_manager_callback_f on_acceptance;
    void *on_acceptance_ref;

    license_manager_callback_f on_rejection;
    void *on_rejection_ref;

    bool _file_found;
    list_t *_pending_licenses; // users should call getter instead of directly accessing this field
} license_manager_t;

void destroy_pending_license(void *ref);

/**
 * Creates a new license manager.
 *
 * #perform_initial_license_check must be called once at an appropriate time after construction while holding
 * XP context.
 *
 * @param directory path where the file tracking license acceptance is to be stored, without trailing separator (string will be copied; original to be managed by caller)
 * @param on_acceptance will be called holding XP context when user has accepted all licenses
 * @param on_acceptance_ref optional argument to be passed to acceptance callback for reference
 * @param on_rejection will be called holding XP context when user has rejected at least one license
 * @param on_rejection_ref optional argument to be passed to rejection callback for reference
 * @return license manager; NULL on error
 */
license_manager_t* create_license_manager(
    char *directory,
    license_manager_callback_f on_acceptance, void *on_acceptance_ref,
    license_manager_callback_f on_rejection, void *on_rejection_ref
);

void destroy_license_manager(license_manager_t *license_manager);

/**
 * Indicates whether all licenses have been accepted by user.
 * @param license_manager license manager
 * @return true if all licenses have been accepted, false if at least one license has not been accepted yet or on error
 */
bool all_licenses_accepted(license_manager_t *license_manager);

/**
 * Indicates whether no (even gone) licenses have been previously accepted.
 * @param license_manager license manager
 * @return true if no license acceptance has been recorded, false if at least one license has been accepted in the past or on error
 */
bool no_licenses_accepted(license_manager_t *license_manager);

/**
 * Lists all licenses which have not been accepted yet in their current revision.
 *
 * Returns a list of pending_license_t; empty if all licenses have been accepted, NULL on error.
 *
 * List and values are to be managed by caller, use #destroy_pending_license for value destruction.
 *
 * @param license_manager license manager
 * @return all licenses not accepted in current revision as list of pending_license_t; empty if all have been accepted, NULL on error
 */
list_t* get_pending_licenses(license_manager_t *license_manager);

/**
 * Checks if the specified license is pending to be accepted, providing details about previous acceptance.
 *
 * pending_license will only be manipulated if successful and will be set to NULL in case the license is known but not
 * pending.
 *
 * Any value set on pending_license is managed by the caller, use #destroy_pending_license for value destruction.
 *
 * @param pending_license if successful: will be set to a copy of the pending license or NULL if the license is not pending
 * @param license_manager license manager
 * @param license_id ID of license to check (case-sensitive)
 * @return error code; #ERROR_NONE on success
 */
error_t get_pending_license(pending_license_t **pending_license, license_manager_t *license_manager, char *license_id);

/**
 * Continues initialization of license manager.
 *
 * Must only be called from XP context; acceptance callback may be called immediately.
 *
 * @param license_manager license manager
 */
void perform_initial_license_check(license_manager_t *license_manager);

/**
 * Marks all currently known licenses as having been accepted by the user, persisting that information on storage.
 * Triggers the acceptance callback; must only be called from XP context (e.g. draw or flight loop callback).
 * @param license_manager license manager
 * @return error code; #ERROR_NONE on success
 */
error_t accept_all_licenses(license_manager_t *license_manager);

/**
 * Revokes previously accepted licenses, removing that information from storage.
 * Triggers the rejection callback; must only be called from XP context (e.g. draw or flight loop callback).
 * @param license_manager license manager
 * @return error code; #ERROR_NONE on success
 */
error_t reject_licenses(license_manager_t *license_manager);

#endif //XPRC_LICENSE_MANAGER_H