// anci c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// libuv
#include <uv.h>

struct halfreaded {
	char * message_buffer = NULL;
	unsigned int message_size = 0;
	int message_size_readed_bytes = 0;
	int message_tail_sz = 0;
};

#define SAS_FREE(ptr) if (ptr) free(ptr), ptr = NULL; 

struct transport_data {
    char * r_buffer = nullptr;
    ssize_t r_buffer_size = 0;
    char * raw_json = nullptr;
    uv_stream_t * handle = nullptr;
};

void close_transport_data(transport_data * tr_data, bool close_handle)
{
    if (tr_data== NULL)
    {
        return;
    }
    
    if (close_handle && tr_data->handle)
    {
        uv_close((uv_handle_t*)tr_data->handle, [](uv_handle_t* handle) { SAS_FREE(handle); });
    }

    SAS_FREE(tr_data->raw_json);
    SAS_FREE(tr_data->r_buffer);
    SAS_FREE(tr_data);
}

void on_write_end(uv_write_t* req, int status)
{
    if (req)
    {
        transport_data* tr_data = (transport_data*)req->data;
        if (status < 0)
        {
            fprintf(stderr, "-SAS- can't send portion of data with error '%s'", uv_strerror(status));
        }
        close_transport_data(tr_data, status < 0); 
    }

    SAS_FREE(req);

    if (status < 0)
    {
        fprintf(stderr, "-SAS- Error on on_write_end callback: %s\n", uv_err_name(status)); 
    }
}
