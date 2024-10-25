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

#define SAS_FREE(ptr) \
free(ptr); \
ptr = NULL; 

struct transport_data {
  char * r_buffer = NULL;
	ssize_t r_buffer_size = 0;
	char * raw_json = NULL;
	uv_stream_t * handle = NULL;
  int uuid = 0;
};

void close_transport_data(transport_data * tr_data, bool close_handle)
{
  if (tr_data== NULL)
  {
    return;
  }
if(close_handle && tr_data->handle)
{
uv_close((uv_handle_t*)tr_data->handle, [](uv_handle_t* handle) { free(handle); });
  }
  SAS_FREE(tr_data->r_buffer);
  SAS_FREE(tr_data);
}

void on_write_end(uv_write_t* req, int status)
{
  if (req)
{
    

    auto tr_data = (transport_data*)req->data;
    #ifdef SAS_DEBUG
    printf("-SAS- on_write_end %ld", tr_data->uuid);
    #endif

close_transport_data(tr_data, status < 0);
  }
if (status < 0)
{
    fprintf(stderr, "-SAS- Error on on_write_end callback: %s\n", uv_err_name(status)); 
  }
}
