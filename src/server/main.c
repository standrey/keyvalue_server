// hashtable implementation
#include "hashtable.h"

#include "error_codes.h"
#include "transport.h"
#include "transport_helpers.h"
#include "fbwrapper.h"

static bool v_mode = false;
const char * usage_strings[] = {
    "  -d Daemonize",
    NULL
};

static uv_tcp_t server;

extern int daemonize();

#define CLIENT_POOL_SIZE 1024
static struct halfreaded * client_pool[CLIENT_POOL_SIZE]; // assume that  uv_default_loop uses one thread 
static struct ht* main_data;

void alloc_buffer(uv_handle_t * handle, size_t size, uv_buf_t *buf) {
    char *base;
    base = (char *)calloc(1, size);
    if(!base)
        *buf = uv_buf_init(NULL, 0);
    else
        *buf = uv_buf_init(base, size);
}

void on_close_free(uv_handle_t* handle) {
    free(handle);
}

void send_command_back(struct transport_data * tr_data) {
    uv_buf_t buf = uv_buf_init(tr_data->r_buffer, tr_data->r_buffer_size);
    uv_write_t * write_req = (uv_write_t *)calloc(1, sizeof(uv_write_t));
    write_req->data = tr_data;

    int res = uv_write(write_req, (uv_stream_t *)tr_data->handle, &buf, 1, on_write_end);
    if (res) {
      fprintf(stderr, "error on sending data: %s\n" , uv_err_name(res));
      close_transport_data(tr_data, true);
      return;  
    }
}

bool process_command(struct transport_data * data) {
    bool isShutdownCommand = false;

    static long total_req = 0;
    static long set_req = 0;
    static long get_hit_req = 0;
    static long get_miss_req = 0;
    static long get_bloommiss_req = 0;

    total_req++;

    char * operation = NULL;
    char * key = NULL;
    char * value = NULL;

    size_t fbBufferSize = 0;
    char * fbBuffer = 0;

    bool fbResult = getFbValues(data, operation, key, value);

    if (!fbResult) {
        free(data->r_buffer);
        data->r_buffer = NULL;
        free(operation);
        free(key);
        free(value);
    }

    if (strcmp(operation,"get")==0) {
        char *str = ht_get(main_data, key);

        if (str) {
            ++get_hit_req;
            setFbValues(ErrorCodes::SUCCESS, "", fbBuffer, fbBufferSize);
        } else {
            ++get_miss_req;
            setFbValues(ErrorCodes::NOT_FOUND, "", fbBuffer, fbBufferSize);
        }
    } else if (strcmp(operation,"set")==0) {
        bool exists = false;
        ++set_req;
        exists = ht_get(main_data, key) != NULL;

        if (exists) {
            setFbValues(ErrorCodes::ALREADY_EXISTS, "", fbBuffer, fbBufferSize);
        } else {
            main_data.set(key, value);
            setFbValues(ErrorCodes::SUCCESS, "", fbBuffer, fbBufferSize);
        }
    }
    else if (strcmp(operation,"stats")==0) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "total_req: %ld, set_req: %ld, get_hit_req: %ld, get_miss_req: %ld, bloom_miss_req: %ld", total_req, set_req, get_hit_req, get_miss_req, get_bloommiss_req);
        setFbValues(ErrorCodes::SUCCESS, buffer, fbBuffer, fbBufferSize);
    }
    else if (strcmp(operation, "shutdown") == 0) {
        setFbValues(ErrorCodes::SUCCESS, "", fbBuffer, fbBufferSize);
        isShutdownCommand = true;
    }

    free(data->r_buffer);
    data->r_buffer = NULL;

    // malloc data for send buffer
    data->r_buffer_size = fbBufferSize + sizeof(int);
    data->r_buffer = (char*)malloc(data->r_buffer_size);
    data->raw_json = NULL;

    // copy int to char buffer
    memcpy(data->r_buffer, &fbBufferSize, 4);

    // copy binary data to buffer
    memcpy(data->r_buffer + sizeof(fbBufferSize), fbBuffer, fbBufferSize);

    free(value);
    free(key);
    free(operation);

    return isShutdownCommand;
}

struct transport_data * read_next_flatbuffer(struct halfreaded msg, ssize_t * sz, const ssize_t nread, const char *base, const uv_stream_t * stream) {
    if (*sz == nread) {
        return NULL;
    }

    ssize_t data_tail_sz = nread - *sz;

    if (msg.message_size_readed_bytes != 4 && msg.message_buffer == NULL) {
        while (*sz != nread && msg.message_size_readed_bytes < 4) {
            msg.message_size_readed_bytes++;
            unsigned char ch = (unsigned char) (*(base + *sz));
            msg.message_size <<= 8;
            msg.message_size |= ch;
            *sz = *sz + 1;
        }
    }

    if (msg.message_size_readed_bytes != 4 || *sz == nread) {
        return NULL;
    }

    data_tail_sz = nread - *sz;

    if (msg.message_buffer == NULL) {
        msg.message_size = ntohl(msg.message_size);
        msg.message_buffer = (char *) calloc(1, msg.message_size);
    }

    if (msg.message_tail_sz > 0) {
        if (msg.message_tail_sz <= data_tail_sz) {
            memcpy(msg.message_buffer, base + *sz, msg.message_tail_sz);
            sz += msg.message_tail_sz;
        } else {
            memcpy(msg.message_buffer + (msg.message_size - data_tail_sz), base + *sz, data_tail_sz);
            msg.message_tail_sz -= data_tail_sz;
            return NULL;
        }
    } else {
        if (msg.message_size <= data_tail_sz) {
            memcpy(msg.message_buffer, base + *sz, msg.message_size);
            sz += msg.message_size;
        } else {
            memcpy(msg.message_buffer, base + *sz, data_tail_sz);
            msg.message_tail_sz = msg.message_size - data_tail_sz;
            return NULL;
        }
    }

    struct transport_data *data = (struct transport_data *)malloc(sizeof(struct transport_data));
    data->r_buffer = (char*)malloc(msg.message_size);
    memcpy(data->r_buffer, msg.message_buffer, msg.message_size);
    data->handle = (uv_stream_t *) stream;
    data->r_buffer_size = msg.message_size;

    SAS_FREE(msg.message_buffer);
    msg.message_size = 0;
    msg.message_size_readed_bytes = 0;
    msg.message_tail_sz = 0;

    return data;
}


struct halfreaded * get_client_by_stream(uv_stream_t * stream) {
    if (stream == NULL) {
        return NULL;
    }

    size_t first_free_index = -1;
    for (size_t i = 0 ; i < CLIENT_POOL_SIZE; ++i) {
        if (client_pool[i] != NULL && client_pool[i]->handle == stream) {
            return client_pool[i];
        }
        if (first_free_index == -1 && client_pool[i] == NULL) {
            first_free_index = i; 
        }
    }

    if (first_free_index == -1) {
        debug("could not allocate new client cell");
        return NULL;
    }

    debug("new client cell allocation");    
    client_pool[first_free_index] = (struct halfreaded *)calloc(1, sizeof(struct halfreaded));
    client_pool[first_free_index]->handle = stream;
    return client_pool[first_free_index];
}

void free_client_by_stream(uv_stream_t * stream) {
    if (stream == NULL) {
        return;
    }
    for (size_t i = 0 ; i < CLIENT_POOL_SIZE; ++i) {
        if (client_pool[i] != NULL && client_pool[i]->handle == stream) {
            free(client_pool[i]);
            client_pool[i] = NULL;
        }
    }
}

void close_stream(uv_stream_t * stream, const uv_buf_t *buf) {
    uv_close((uv_handle_t*)stream, on_close_free);
}

void close_all_client_streams() {
    for (size_t i = 0 ; i < CLIENT_POOL_SIZE; ++i) {
        if (client_pool[i] != NULL) {
            free(client_pool[i]);
            client_pool[i] = NULL;
        }
    }
}

void read_cb(uv_stream_t * stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread <= 0) {
        if (nread != UV_EOF) {
            fail("read error: %s", uv_strerror(nread));
        }
        free_client_by_stream(stream);
        close_stream(stream, buf);
        return;
    }

    ssize_t processed_bytes = 0;
    struct transport_data * request_data = NULL;
    bool isShutdownCommand = false;

    struct halfreaded * read_transport = get_client_by_stream(stream);
    if (!read_transport) {
        free_client_by_stream(stream);
        uv_close((uv_handle_t*)stream, on_close_free);
        if (buf->base) free((void*)buf->base);
        fail("can't read data from the client");
    }

    do {
        request_data = read_next_flatbuffer(*read_transport, &processed_bytes, nread, buf->base, stream);
        isShutdownCommand = process_command(request_data);
        debug("request buffer transport size %ld", request_data->r_buffer_size);
        send_command_back(request_data);
    } while (request_data != NULL && !isShutdownCommand && processed_bytes<nread);

    debug("all the commands have been processed");

    uv_close((uv_handle_t*)stream, on_close_free);
    if (buf->base) free((void*)buf->base);

    if (isShutdownCommand) {
        debug("shutting down");
        close_all_client_streams();
        uv_close((uv_handle_t*)&server, NULL);
        uv_loop_close(uv_default_loop());
    }
}

void connection_cb(uv_stream_t * server, int status) {
    if (status < 0) {
        fprintf(stderr, "connection_cb error: %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t * client = (uv_tcp_t *)calloc(1, sizeof(uv_tcp_t));
    status = uv_tcp_init(uv_default_loop(), client);
    if (status < 0) {
        fprintf(stderr, "can't call uv_tcp_init with error [%s]\n", uv_strerror(status));
        return;
    }

    if (uv_accept(server, (uv_stream_t *) client) == 0) {
        uv_read_start((uv_stream_t *) client, alloc_buffer, read_cb);
    } else {
        uv_close((uv_handle_t *)client, on_close_free);
    }
}

void on_uv_close(uv_handle_t* handle) {
    if (handle != NULL) {
        SAS_FREE(handle);
    }
}


void on_uv_walk(uv_handle_t * handle, void * arg) {
    uv_close(handle, on_uv_close);
}

void on_sigint_received(uv_signal_t * handle, int signum) {
    int res = uv_loop_close(handle->loop);
    if (res == UV_EBUSY) { 
        uv_walk(handle->loop, on_uv_walk, NULL);
    }
}

int main(int argc, char *argv[]) {
    int i;
    int arg_count = 1;
    for (; arg_count < argc; ++arg_count){
        switch(argv[arg_count][1]) {
            case 'd':
                {
                    daemonize();
                    break;
                }
            default:
                {
                    i = 0;
                    while(usage_strings[i] != NULL) {
                        printf(usage_strings[i]);
                        i++;
                    }
                    break;
                }
        }
    }

    //struct uv_signal_t *sigint = calloc(1, sizeof(uv_signal_t));
    //uv_signal_init(loop, sigint);
    //uv_signal_start(sigint, on_sigint_received, SIGINT);

    main_data = ht_make();
    struct sockaddr_in addr;
    uv_ip4_addr("127.0.0.1", 7000, &addr);
    uv_tcp_init(uv_default_loop(), &server);
    uv_tcp_bind(&server, (struct sockaddr *)&addr, 0);
    int r = uv_listen((uv_stream_t *) &server, 128, connection_cb);
    if (r) {
        fprintf(stderr, "Error on listening: %s\n", uv_strerror(r));
        return r;
    }
    int res =  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    ht_destroy(main_data);
    return res;
}


