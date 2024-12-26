#pragma once

// anci c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// libuv
#include <uv.h>

#include "transport.h"

#define SAS_FREE(ptr) if (ptr) free(ptr), ptr = NULL; 
#define fail(...) do { fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE); } while (0)

#ifdef DEBUG
#define debug(...) fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n")
#else 
#define debug(...) 
#endif

void after_uv_close(uv_handle_t* handle) {
    SAS_FREE(handle);
}

void close_transport_data(struct transport_data * tr_data, bool close_handle) {
    if (tr_data== NULL) {
        return;
    }
    
    if (close_handle && tr_data->handle) {
        uv_close((uv_handle_t*)tr_data->handle,  after_uv_close);
    }

    SAS_FREE(tr_data->raw_json);
    SAS_FREE(tr_data->r_buffer);
    SAS_FREE(tr_data);
}

void on_write_end(uv_write_t * req, int status) {
    if (req) {
        struct transport_data* tr_data = (struct transport_data*)req->data;
        if (status < 0) {
            fprintf(stderr, "-SAS- can't send portion of data with error '%s'", uv_strerror(status));
        }
        close_transport_data(tr_data, status < 0); 
    }

    SAS_FREE(req);

    if (status < 0) {
        fprintf(stderr, "-SAS- Error on on_write_end callback: %s\n", uv_err_name(status)); 
    }
}
