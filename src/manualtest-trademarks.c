#include <stdio.h>

#include "trademarks.h"

int main(int argc, char **argv) {
    list_t *trademarks = xprc_get_trademarks_acknowledgments();
    if (!trademarks) {
        printf("failed to retrieve list of trademarks and acknowledgments\n");
        return 1;
    }

    printf("Trademarks and acknowledgments:\n");
    int index = 0;
    for (list_item_t *item = trademarks->head; item; item = item->next, index++) {
        printf("\n");
        printf("#%02d:\n", index);
        printf("%s\n", (char*)item->value);
    }

    destroy_list(trademarks, NULL);

    return 0;
}
