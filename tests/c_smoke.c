#include "rdp/sdk.h"

#include <assert.h>
#include <string.h>

int main(void) {
    assert(rdp_version() != 0);

    rdp_config_t* config = rdp_config_new();
    assert(config != 0);

    rdp_error_t* err = 0;
    assert(rdp_config_set_server_url(config, "https://rdp.local", &err) == 0);
    assert(err == 0);

    assert(rdp_config_set_bearer_token(config, "tok-123", &err) == 0);
    assert(err == 0);

    rdp_client_t* client = 0;
    assert(rdp_client_new(config, &client, &err) == 0);
    assert(err == 0);
    assert(client != 0);

    rdp_buffer_t rpc_body = {0};
    assert(rdp_client_rpc_unary(client, "/raft.wdm.v1.service.ObjectService/SearchObjects", "", 0, &rpc_body, &err) !=
           0);
    assert(strstr(rdp_error_message(err), "WDM RPC support") != 0);
    rdp_error_free(err);
    err = 0;

    rdp_client_free(client);
    rdp_config_free(config);
    return 0;
}
