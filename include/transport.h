#pragma once 

// anci c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// libuv
#include <uv.h>

struct halfreaded {
    uv_stream_t * handle;
	char * message_buffer;
	unsigned int message_size;
	int message_size_readed_bytes;
	int message_tail_sz;
};


struct transport_data {
    char * r_buffer;
    ssize_t r_buffer_size;
    char * raw_json;
    uv_stream_t * handle;
};


