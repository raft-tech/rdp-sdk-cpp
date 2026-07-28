/*
 * Run:
 *
 *   cmake -S . -B build -DRDP_SDK_ENABLE_WDM=ON
 *   cmake --build build --target objects-search-c
 *   export RDP_SERVER_URL=https://rdp.example.com
 *   export RDP_API_KEY=your-api-key
 *   ./build/objects-search-c
 */

#include "rdp/sdk.h"

#include <stdio.h>

#if !RDP_SDK_ENABLE_WDM
int main(void) {
    fprintf(stderr, "objects-search-c requires a build with RDP_SDK_ENABLE_WDM=ON\n");
    return 2;
}
#else
int main(void) {
    rdp_error_t* err = 0;
    rdp_config_t* config = 0;
    if (rdp_config_load(&config, &err) != 0) {
        fprintf(stderr, "%s\n", rdp_error_message(err));
        rdp_error_free(err);
        return 1;
    }

    rdp_client_t* client = 0;
    if (rdp_client_new(config, &client, &err) != 0) {
        fprintf(stderr, "%s\n", rdp_error_message(err));
        rdp_error_free(err);
        rdp_config_free(config);
        return 1;
    }

    rdp_buffer_t body = {0};
    const unsigned char request[] = {0x18, 0x0a}; /* SearchObjectsRequest page_size = 10 */
    if (rdp_client_rpc_unary(client, "/raft.wdm.v1.service.ObjectService/SearchObjects", request, sizeof(request),
                             &body, &err) != 0) {
        fprintf(stderr, "%s\n", rdp_error_message(err));
        rdp_error_free(err);
        rdp_client_free(client);
        rdp_config_free(config);
        return 1;
    }

    printf("SearchObjects response: %zu bytes\n", body.len);
    rdp_buffer_free(&body);
    rdp_client_free(client);
    rdp_config_free(config);
    return 0;
}
#endif
