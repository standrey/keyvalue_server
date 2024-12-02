// hashtable implementation
#include "hashtable.h"

#include <map>

// flatbuffers
#include "request_generated.h"
#include "reply_generated.h"
#include "error_codes.h"
#include "transport.h"

static bool v_mode = false;
const char * usage_strings[] = {
    "  -d Daemonize",
    "  -v Verbose mode",
    NULL
};

uv_mutex_t mutex;
static uv_loop_t * loop;
static uv_tcp_t server;

std::map<uv_stream_t*, halfreaded> streams;

extern int daemonize(const char *);

struct threadsafe_hashtable {
    struct ht * table = NULL;

    void set(const char *k, const char *v) {
        if (table == NULL) {
            table = ht_make();
        }
        ht_set(table, k, strdup(v));
    }

    bool exists(const char *k) {
        if (table == NULL) {
            assert("hash table is not initalized");
            return false;
        }

        return ht_get(table, k) != NULL;
    }

    char* get(const char* k)
    {
        if (table == NULL) {
            return NULL;
        }

        return (char *)ht_get(table, k);
    }

};

static threadsafe_hashtable main_data;

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

void send_command_back(transport_data * tr_data) {
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

bool process_command(transport_data * data) {
    bool isShutdownCommand = false;
    static flatbuffers::FlatBufferBuilder builder(512);

    static long total_req = 0;
    static long set_req = 0;
    static long get_hit_req = 0;
    static long get_miss_req = 0;
    static long get_bloommiss_req = 0;
    auto request = Homework::GetRequest(data->r_buffer);
    if (!request) {
        free(data->r_buffer), data->r_buffer = NULL;
        if (v_mode) printf("\tReceived flatbuffer is empty or null\n");
        return isShutdownCommand;
    }

    ++total_req;
    const char *operation = request->operation()->c_str();
    if (v_mode) printf("\tprocess_command [%s] %s->%s\n", operation, request->member()->key()->c_str(), request->member()->value()->c_str());
    if (v_mode) printf("\tRequest buffer transport size %ld\n", data->r_buffer_size);

    if (strcmp(operation,"get")==0) {
        char *str = NULL;
        {
            str = main_data.get(request->member()->key()->c_str());
        }

        if (str) {
            ++get_hit_req;
            auto realdata = builder.CreateString(str);
            auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::SUCCESS, realdata);
            builder.Finish(reply);
        } else {
            ++get_miss_req;
            auto realdata = builder.CreateString("");
            auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::NOT_FOUND, realdata);
            builder.Finish(reply);
        }
    }
    else if (strcmp(operation,"set")==0) {
        bool exists = false;
        ++set_req;
        exists = main_data.exists(request->member()->key()->c_str());

        if (exists) {
            auto realdata = builder.CreateString("");
            auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::ALREADY_EXISTS, realdata);
            builder.Finish(reply);
        } else {
            main_data.set(request->member()->key()->c_str(), request->member()->value()->c_str());
            auto realdata = builder.CreateString("");
            auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::SUCCESS, realdata);
            builder.Finish(reply);
        }
    }
    else if (strcmp(operation,"stats")==0) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "total_req: %ld, set_req: %ld, get_hit_req: %ld, get_miss_req: %ld, bloom_miss_req: %ld", total_req, set_req, get_hit_req, get_miss_req, get_bloommiss_req);
        auto realdata = builder.CreateString(buffer);
        auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::SUCCESS, realdata);
        builder.Finish(reply);
    }
    else if (strcmp(operation, "shutdown") == 0) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "0");
        auto realdata = builder.CreateString(buffer);
        auto reply = Homework::CreateReply(builder, (int32_t)ErrorCodes::SUCCESS, realdata);
        builder.Finish(reply);
        isShutdownCommand = true;
    }

    SAS_FREE(data->r_buffer);

    // malloc data for send buffer
    int sz = (int)builder.GetSize();
    data->r_buffer_size = sz + sizeof(int);
    data->r_buffer = (char*)malloc(data->r_buffer_size);
    data->raw_json = nullptr;

    // copy int to char buffer
    memcpy(data->r_buffer, &sz, 4);

    // copy binary data to buffer
    memcpy(data->r_buffer + sizeof(sz), (void *)builder.GetBufferPointer(), sz);

    builder.Clear();

    return isShutdownCommand;
}

transport_data * read_next_flatbuffer(uv_stream_t * stream, ssize_t & sz, const ssize_t nread, const char *base) {
    if (streams.find(stream) == streams.end()) {
        streams[stream] = halfreaded{0, 0, 0, 0};
    }

    halfreaded & msg = streams[stream];

    if (sz == nread) {
        return NULL;
    }

    ssize_t data_tail_sz = nread - sz;

    if (msg.message_size_readed_bytes != 4 && msg.message_buffer == NULL) {
        while (sz != nread && msg.message_size_readed_bytes < 4) {
            msg.message_size_readed_bytes++;
            unsigned char ch = (unsigned char) (*(base + sz));
            msg.message_size <<= 8;
            msg.message_size |= ch;
            ++sz;
        }
    }

    if (msg.message_size_readed_bytes != 4 || sz == nread) {
        return NULL;
    }

    data_tail_sz = nread - sz;

    if (msg.message_buffer == NULL) {
        msg.message_size = ntohl(msg.message_size);
        msg.message_buffer = (char *) calloc(1, msg.message_size);
    }

    if (msg.message_tail_sz > 0) {
        if (msg.message_tail_sz <= data_tail_sz) {
            memcpy(msg.message_buffer, base + sz, msg.message_tail_sz);
            sz += msg.message_tail_sz;
        } else {
            memcpy(msg.message_buffer + (msg.message_size - data_tail_sz), base + sz, data_tail_sz);
            msg.message_tail_sz -= data_tail_sz;
            return NULL;
        }
    } else {
        if (msg.message_size <= data_tail_sz) {
            memcpy(msg.message_buffer, base + sz, msg.message_size);
            sz += msg.message_size;
        } else {
            memcpy(msg.message_buffer, base + sz, data_tail_sz);
            msg.message_tail_sz = msg.message_size - data_tail_sz;
            return NULL;
        }
    }

    transport_data *data = (transport_data *)malloc(sizeof(transport_data));
    data->r_buffer = (char*)malloc(msg.message_size);
    memcpy(data->r_buffer, msg.message_buffer, msg.message_size);
    data->handle = (uv_stream_t *) stream;
    data->r_buffer_size = msg.message_size;

    msg.message_buffer = NULL;
    msg.message_size = 0;
    msg.message_size_readed_bytes = 0;
    msg.message_tail_sz = 0;

    return data;
}

void read_cb(uv_stream_t * stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread != UV_EOF)
        {
            fprintf(stderr, "read error:%s\n", uv_strerror(nread));
        }

        if (streams.find(stream) != streams.end()) {
            free(streams[stream].message_buffer);
            streams.erase(stream);
        }
        uv_close((uv_handle_t*)stream, on_close_free);
        free(buf->base);
        return;
    } else if (nread == 0) {
        free(buf->base);
        return;
    }

    ssize_t readed_bytes = 0;
    static int uid = 0;
    transport_data * request_data = NULL;
    bool isShutdownCommand = false;
    while (request_data = read_next_flatbuffer(stream, readed_bytes, nread, buf->base)) {
        isShutdownCommand = process_command(request_data);
        if (v_mode) {
            printf("\tRequest buffer transport size %ld\n", request_data->r_buffer_size);
        }

        send_command_back(request_data);
        if (isShutdownCommand) {
            if (v_mode) {
                printf("\tSuccessfully terminated.\n");
            }
            break;
        }
    }

    free(buf->base);
    uv_close((uv_handle_t*)stream, on_close_free);
}

void connection_cb(uv_stream_t * server, int status) {
    if (status < 0) {
        fprintf(stderr, "connection_cb error: %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t * client = (uv_tcp_t *)calloc(1, sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);

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
                    daemonize("/run/serverclient.pid");
                    break;
                }
            case 'v':
                {
                    v_mode = true;
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

    loop = uv_default_loop();

    //struct uv_signal_t *sigint = calloc(1, sizeof(uv_signal_t));
    //uv_signal_init(loop, sigint);
    //uv_signal_start(sigint, on_sigint_received, SIGINT);

    struct sockaddr_in addr;
    uv_ip4_addr("127.0.0.1", 7000, &addr);
    uv_tcp_init(loop, &server);
    uv_tcp_bind(&server, (struct sockaddr *)&addr, 0);
    int r = uv_listen((uv_stream_t *) &server, 128, connection_cb);
    if (r) {
        fprintf(stderr, "Error on listening: %s\n", uv_strerror(r));
        return r;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}

