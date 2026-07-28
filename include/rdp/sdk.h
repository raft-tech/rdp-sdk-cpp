#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rdp_config rdp_config_t;
typedef struct rdp_client rdp_client_t;
typedef struct rdp_error rdp_error_t;

typedef struct rdp_buffer {
    char* data;
    size_t len;
} rdp_buffer_t;

const char* rdp_version(void);

rdp_config_t* rdp_config_new(void);
int rdp_config_load(rdp_config_t** out, rdp_error_t** err);
void rdp_config_free(rdp_config_t* config);

int rdp_config_set_server_url(rdp_config_t* config, const char* value, rdp_error_t** err);
int rdp_config_set_api_key(rdp_config_t* config, const char* value, rdp_error_t** err);
int rdp_config_set_bearer_token(rdp_config_t* config, const char* value, rdp_error_t** err);
int rdp_config_set_client_credentials(rdp_config_t* config, const char* client_id, const char* client_secret,
                                      rdp_error_t** err);
void rdp_config_set_tls_skip_verify(rdp_config_t* config, bool enabled);

int rdp_client_new(const rdp_config_t* config, rdp_client_t** out, rdp_error_t** err);
void rdp_client_free(rdp_client_t* client);

int rdp_client_request(rdp_client_t* client, const char* method, const char* path, const char* body, rdp_buffer_t* out,
                       long* status_code, rdp_error_t** err);

int rdp_client_rpc_unary(rdp_client_t* client, const char* full_method, const void* request_data, size_t request_len,
                         rdp_buffer_t* out, rdp_error_t** err);

void rdp_buffer_free(rdp_buffer_t* buffer);

const char* rdp_error_message(const rdp_error_t* err);
void rdp_error_free(rdp_error_t* err);

#ifdef __cplusplus
}
#endif
