#include <stdio.h>
#include <stdlib.h>

#include "network.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("call with 1 to include IPv6 addresses, 0 to only show IPv4\n");
        return 1;
    }
    bool include_ipv6 = (atoi(argv[1]) == 1);

    error_t err = initialize_os_network_apis();
    if (err != ERROR_NONE) {
        printf("failed to initialize OS network APIs: %d\n", err);
        return 1;
    }

    list_t *interfaces = get_network_interfaces(include_ipv6);
    if (!interfaces) {
        printf("no additional interfaces found\n");
        return 1;
    }

    list_item_t *item = interfaces->head;
    int i = 0;
    while (item) {
        printf("%2d %s\n", i++, (char*) item->value);
        item = item->next;
    }

    destroy_list(interfaces, free);

    return 0;
}