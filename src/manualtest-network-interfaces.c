#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "network.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("call with 1 to include IPv6 addresses, 0 to only show IPv4\n");
        return 1;
    }
    bool include_ipv6 = (atoi(argv[1]) == 1);

    xprc_log_init();
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);

    error_t err = initialize_os_network_apis();
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to initialize OS network APIs: %d", err);
        return 1;
    }

    list_t *interfaces = get_network_interfaces(include_ipv6);
    if (!interfaces) {
        RCLOG_INFO("no interfaces found");
        return 1;
    }

    list_item_t *item = interfaces->head;
    int i = 0;
    while (item) {
        RCLOG_INFO("%2d %s", i++, (char*) item->value);
        item = item->next;
    }

    destroy_list(interfaces, free);

    xprc_log_destroy();

    return 0;
}