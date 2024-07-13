#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lists.h"

void fail(char *reason) {
    printf(" => failed (%s)\n", reason);
    exit(1);
}

void setup_fail(char *description) {
    printf(" => setup failed: %s\n", description);
    exit(1);
}

void testcase(char *description) {
    printf("test: %s\n", description);
}

int cmplong_direct_value(list_item_t *item_a, list_item_t *item_b) {
    /*
    // "partition" function over current implementation is expected to cross over indices
    if (item_a == item_b) {
        fail("comparator called with same item on both sides");
    }
    */

    if (!item_a) {
        fail("comparator called with NULL for item A");
    }

    if (!item_b) {
        fail("comparator called with NULL for item B");
    }

    long value_a = (long) item_a->value;
    if ((void*)value_a != item_a->value) {
        fail("direct long value is expected to be stored on items instead of pointers but wasn't for item A");
    }

    long value_b = (long) item_b->value;
    if ((void*)value_b != item_b->value) {
        fail("direct long value is expected to be stored on items instead of pointers but wasn't for item B");
    }

    if (value_a > value_b) {
        return 1;
    } else if (value_a < value_b) {
        return -1;
    } else {
        return 0;
    }
}

void append_direct_values(list_t *list, int count, long *values) {
    for (int i=0; i<count; i++) {
        if (!list_append(list, (void*)(values[i]))) {
            setup_fail("failed to append item to input list");
        }
    }
}

void assert_equal_direct_values(list_t *actual, int num_expected, long *expected_values) {
    if (!actual) {
        fail("actual list is NULL");
    }

    list_item_t *it = actual->head;
    int i = 0;
    bool success = true;

    if (actual->size != num_expected) {
        printf(" => expected list with %d items but got %d\n", num_expected, actual->size);
        fail("unexpected list size");
    }

    while (it) {
        if ((long) it->value != expected_values[i]) {
            printf(" => index %d does not match, expected %ld, got %ld\n", i, expected_values[i], (long) it->value);
            success = false;
        }

        i++;
        it = it->next;
    }

    if (success) {
        return;
    }

    printf(" => mismatch detected, full dump:\n");
    i = 0;
    it = actual->head;
    while (it) {
        printf("    i=%d, expected=%ld, actual=%ld\n", i, expected_values[i], (long) it->value);

        i++;
        it = it->next;
    }

    fail("returned list with unexpected value");
}

void assert_ascending_direct_values(list_t *actual, int num_expected) {
    if (!actual) {
        fail("actual list is NULL");
    }

    list_item_t *it = actual->head;
    int i = 0;
    bool success = true;

    if (actual->size != num_expected) {
        printf(" => expected list with %d items but got %d\n", num_expected, actual->size);
        fail("unexpected list size");
    }

    long previous_value = 0;
    while (it) {
        long value = (long) it->value;
        if (i != 0) {
            if (value < previous_value) {
                printf(" => index %d: expected >= %ld, got %ld\n", i, previous_value, value);
                success = false;
            }
        }

        i++;
        it = it->next;
        previous_value = value;
    }

    if (success) {
        return;
    }

    printf(" => mismatch detected, full dump:\n");
    i = 0;
    it = actual->head;
    while (it) {
        printf("    i=%d: %ld\n", i, (long) it->value);

        i++;
        it = it->next;
    }

    fail("list items are not in ascending order of values");
}

const long single_value[] = {452315};

#define NUM_STATIC_VALUES 15
const long static_unsorted[] = {
        10, -20, 0, 9, 1,
        -15, 34, 21, -9, -8,
        -10, -15, 9, 20, -20
};
const long static_sorted[] = {
        -20, -20, -15, -15, -10,
        -9, -8, 0, 1, 9,
        9, 10, 20, 21, 34
};

int main(int argc, char **argv) {
    list_t *in = NULL;
    list_t *res = NULL;
    list_t *list = NULL;
    list_item_t *item = NULL;
    list_item_t *prev_item = NULL;

    printf("--- list tests starting\n");

    printf("-- list_prepend\n");
    testcase("empty list");
    list = create_list();
    if (!list_prepend(list, (void*)42)) {
        fail("failed to prepend item (function returned false)");
    }
    if (list->size != 1) {
        fail("unexpected list size after prepend");
    }
    if (list->head != list->tail) {
        fail("head and tail do not match after prepend");
    }
    if (!list->head) {
        fail("head is NULL after prepend");
    }
    if (list->head->next) {
        fail("first item next pointer is NOT NULL after prepend");
    }
    if (list->head->prev) {
        fail("first item prev pointer is NOT NULL after prepend");
    }
    if (list->head->value != (void*)42) {
        fail("unexpected value on first item after prepend");
    }
    destroy_list(list, NULL);
    list = NULL;

    testcase("existing list");
    list = create_list();
    if (!list_append(list, (void*)23)) {
        setup_fail("failed to add first item (function returned false)");
    }
    if (!list_append(list, (void*)42)) {
        setup_fail("failed to add second item (function returned false)");
    }
    if (!list_append(list, (void*)11)) {
        setup_fail("failed to add third item (function returned false)");
    }
    if (!list_prepend(list, (void*)5)) {
        fail("failed to prepend item (function returned false)");
    }
    if (list->size != 4) {
        fail("unexpected list size after prepend");
    }
    item = list->head;
    prev_item = NULL;
    if (item->value != (void*)5) {
        fail("unexpected value on first item (prepended)");
    }
    if (item->prev) {
        fail("prev pointer is NOT NULL on first item (prepended)");
    }
    prev_item = item;
    item = item->next;
    if (!item) {
        fail("next pointer is NULL on first item (prepended)");
    }
    if (item->prev != prev_item) {
        fail("prev pointer of second item (originally first appended) does not link to first item (prepended)");
    }
    if (item->value != (void*)23) {
        fail("unexpected value on second item (originally first appended)");
    }
    prev_item = item;
    item = item->next;
    if (!item) {
        fail("next pointer is NULL on second item (originally first appended)");
    }
    if (item->prev != prev_item) {
        fail("prev pointer of third item (originally second appended) does not link to second item (originally first appended)");
    }
    if (item->value != (void*)42) {
        fail("unexpected value on third item (originally second appended)");
    }
    prev_item = item;
    item = item->next;
    if (!item) {
        fail("next pointer is NULL on third item (originally second appended)");
    }
    if (item->prev != prev_item) {
        fail("prev pointer of fourth item (originally third appended) does not link to third item (originally second appended)");
    }
    if (item->value != (void*)11) {
        fail("unexpected value on fourth item (originally third appended)");
    }
    if (item->next) {
        fail("next pointer is NOT NULL on fourth item (originally third appended) although it should be the last one");
    }
    if (list->tail != item) {
        fail("list tail does not point to last item");
    }
    destroy_list(list, NULL);
    list = NULL;
    item = NULL;
    prev_item = NULL;

    printf("-- copy_list_sorted\n");

    testcase("NULL list");
    if (copy_list_sorted(NULL, cmplong_direct_value)) {
        fail("result was not NULL");
    }

    testcase("test: empty list");
    in = create_list();
    if (!in) {
        setup_fail("list could not be created");
    }
    res = copy_list_sorted(in, cmplong_direct_value);
    if (!res) {
        fail("returned NULL");
    }
    if (res->size != 0) {
        printf(" => got %d items", res->size);
        fail("returned non-empty list");
    }
    destroy_list(in, NULL);
    destroy_list(res, NULL);
    in = NULL;
    res = NULL;

    testcase("single-item list");
    in = create_list();
    if (!in) {
        setup_fail("list could not be created");
    }
    append_direct_values(in, 1, (long*) single_value);
    res = copy_list_sorted(in, cmplong_direct_value);
    assert_equal_direct_values(res, 1, (long*) single_value);
    destroy_list(in, NULL);
    destroy_list(res, NULL);
    in = NULL;
    res = NULL;

    testcase("static unsorted list");
    in = create_list();
    if (!in) {
        setup_fail("list could not be created");
    }
    append_direct_values(in, NUM_STATIC_VALUES, (long*) static_unsorted);
    res = copy_list_sorted(in, cmplong_direct_value);
    assert_equal_direct_values(res, NUM_STATIC_VALUES, (long*) static_sorted);
    destroy_list(in, NULL);
    destroy_list(res, NULL);
    in = NULL;
    res = NULL;

    testcase("static sorted list");
    in = create_list();
    if (!in) {
        setup_fail("list could not be created");
    }
    append_direct_values(in, NUM_STATIC_VALUES, (long*) static_sorted);
    res = copy_list_sorted(in, cmplong_direct_value);
    assert_equal_direct_values(res, NUM_STATIC_VALUES, (long*) static_sorted);
    destroy_list(in, NULL);
    destroy_list(res, NULL);
    in = NULL;
    res = NULL;

    testcase("NULL comparator");
    in = create_list();
    if (!in) {
        setup_fail("list could not be created");
    }
    append_direct_values(in, NUM_STATIC_VALUES, (long*) static_unsorted);
    res = copy_list_sorted(in, NULL);
    if (res) {
        fail("result was not NULL");
    }
    destroy_list(in, NULL);
    in = NULL;

    // prepare for "fuzzy" runs using random input
    testcase("random lists");
    int num_runs = 1000;
    int max_length = 150;
    srandom(time(NULL));

    for (int run=0; run<num_runs; run++) {
        int length = abs((int) (random() % max_length));
        long *unsorted = malloc(length * sizeof(long));
        if (!unsorted) {
            setup_fail("failed to allocate unsorted array");
        }
        for (int i = 0; i < length; i++) {
            unsorted[i] = random();
        }
        in = create_list();
        if (!in) {
            setup_fail("list could not be created");
        }
        append_direct_values(in, length, unsorted);
        res = copy_list_sorted(in, cmplong_direct_value);
        assert_ascending_direct_values(res, length);
        destroy_list(in, NULL);
        destroy_list(res, NULL);
        free(unsorted);
    }

    printf("--- list tests completed\n");
    return 0;
}