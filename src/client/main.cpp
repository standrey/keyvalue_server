// cpp
#include <string>
#include <thread>
#include <string_view>
#include <fstream>
#include <chrono>

// flatbuffer
#include "reply_generated.h"
#include "request_generated.h"
#include <flatbuffers/util.h>
#include <flatbuffers/idl.h>

#include "error_codes.h"
#include "transport.h"
#include "ringbuffer.hpp"

Homework::ringbuffer<transport_data*, 124> container;
flatbuffers::Parser reply_parser;

std::string_view json_input_file;
std::string_view fbs_request_file;
std::string_view fbs_reply_file;

std::atomic<bool> work_finished {false};
ssize_t reply_counter = 0;
ssize_t request_counter = 0;

static void Help(void)
{
  printf("Usage:\n\n");
  printf(" client [fbs_request] [fbs_reply] [json_commands]...\n");
  
  printf("Command file options:\n");
  printf(" --json_commands ...... json commands file\n");
  printf(" --fbs_request ........ flatbuffer request file\n");
  printf(" --fbs_reply .......... flatbuffer reply file\n");
  printf("\n");
}

void end_work_callback(uv_work_t *req, int status) {
    SAS_FREE(req);
}

void on_close_free(uv_handle_t* handle) {
    SAS_FREE(handle);
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;

#ifdef SAS_DEBUG
    fprintf(stderr, "\n-SAS- alloc_buffer\n");
#endif
}

void process_command(transport_data * tr_data) {
    if (tr_data->r_buffer == nullptr) {
        fprintf(stderr, "\n -SAS- Empty response buffer, nothing to parse\n");
        return;
    }


    auto reply = Homework::GetReply(tr_data->r_buffer); 
    if (reply == nullptr) {  
        fprintf(stderr, "\n -SAS- Can't parse binary buffer\n");
        return;
    }

    printf("\n[Reply] %d %s\n", reply->code(), reply->value()->c_str());
}

transport_data * read_next_flatbuffer(uv_stream_t * stream, ssize_t & sz, const ssize_t nread, const char *base) {
    static halfreaded msg;

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

    transport_data *tr_data = (transport_data *)calloc(1, sizeof(transport_data));
    tr_data->r_buffer = (char*)malloc(msg.message_size);
    memcpy(tr_data->r_buffer, msg.message_buffer, msg.message_size);
    tr_data->r_buffer_size = msg.message_size;

    free(msg.message_buffer);
    msg.message_buffer = 0;
    msg.message_size = 0;
    msg.message_size_readed_bytes = 0;
    msg.message_tail_sz = 0;

    return tr_data;
}

void on_response_callback_stop(uv_handle_t * server, const uv_buf_t * buf) {
    void * free_buf = buf->base;
    SAS_FREE(free_buf);
}

void on_response_callback(uv_stream_t * server, ssize_t nread, const uv_buf_t * buf) {
    if (nread < 0) {
        fprintf(stderr, "-SAS- error: %s\n", uv_strerror(nread));
        //on_response_callback_stop((uv_handle_t*)server, buf);
    } else if (nread == 0) {
    } else {
	    ssize_t readed_bytes = 0;
    	transport_data * tr_data = nullptr;

        while (tr_data = read_next_flatbuffer(server, readed_bytes, nread, buf->base)) {
            process_command(tr_data);
    		close_transport_data(tr_data, false);
        }

        if (reply_counter == request_counter && work_finished) {
            //on_response_callback_stop((uv_handle_t*)server, buf);
        }
    }
    on_response_callback_stop((uv_handle_t*)server, buf);
}

void make_fb_command(transport_data * tr_data, flatbuffers::Parser & request_parser) {
    bool ok = request_parser.Parse(tr_data->raw_json);
    if (!ok) {
        fprintf(stderr, "can't parse JSON line \n\t %s\n", tr_data->raw_json);
        return;
    }

    // calculate sizes
    int message_buffer_size = (int) request_parser.builder_.GetSize();
    int transport_buffer_size = sizeof(char) * message_buffer_size + sizeof(int);

    // allocate memory
    char *transport_buffer = (char *)calloc(1, transport_buffer_size);

    // copy int to char buffer
    memcpy(transport_buffer, &message_buffer_size, 4);

    // copy binary data from flatbuffer to char buffer
    memcpy(transport_buffer + sizeof(int), (void *) request_parser.builder_.GetBufferPointer(), message_buffer_size);
    tr_data->r_buffer = transport_buffer;
    tr_data->r_buffer_size = transport_buffer_size;
}

void send_fb_command(transport_data * tr_data) {
    uv_buf_t buf = uv_buf_init(tr_data->r_buffer, tr_data->r_buffer_size);
    uv_write_t * write_req = (uv_write_t *)calloc(1, sizeof(uv_write_t));
    write_req->data = tr_data;

#ifdef SAS_DEBUG
    printf("[Request #%ld]\t%s\n", request_counter++, tr_data->raw_json);
#endif
  
    int buf_count = 1;
    int res = uv_write(write_req, (uv_stream_t *)tr_data->handle, &buf, buf_count, on_write_end);
    if (res) {
        fprintf(stderr, "error on_connect: %s\n", uv_err_name(res));
        return;
    }
}

void requests_queue_runner(uv_work_t *work_request) {
    work_finished = false;
    
    std::fstream in;
    in.open(json_input_file.data(), std::ios::in);
    if (!in.is_open()) {
        return;
    }

    std::string json_line;
    while (std::getline(in, json_line)) {
        transport_data *tr_data = (transport_data*)calloc(1, sizeof(transport_data));
        tr_data->handle = (uv_stream_t *)work_request->data;
        tr_data->raw_json = (char*)calloc(1, json_line.size() + 1);
        strcpy(tr_data->raw_json, json_line.data());
        container.push(tr_data);
    }

    work_finished = true;
}

void sender_queue_runner(uv_work_t *work_request) {
    transport_data * data_ptr = nullptr;

    flatbuffers::Parser request_parser;
    std::string request_schemafile;
    bool ok = flatbuffers::LoadFile(fbs_request_file.data(), false, &request_schemafile);
    if (!ok) {
        fprintf(stderr, "Failed to load fbs request file '%s'\n", fbs_request_file);
        return;
    }

    ok = request_parser.Parse(request_schemafile.c_str());
    if (!ok) {
        fprintf(stderr, "Can't parse request schema");
        return;
    }


    while (container.size() || !work_finished) {
        bool result_ready = container.pop(data_ptr);
        if (!result_ready) {
            std::this_thread::yield();
            return;
        }

        make_fb_command(data_ptr, request_parser);
        send_fb_command(data_ptr);
    }
}

void on_connect(uv_connect_t * connection, int status) {
    if (status < 0) {
        fprintf(stderr, "SAS can't connect to server: '%s'\n", uv_strerror(status));
        return;
    }

    uv_stream_t * tcp = (uv_stream_t *)connection->handle;
    uv_work_t * work_request = (uv_work_t *)calloc(1, sizeof(uv_work_t));
    work_request->data = tcp;
    uv_queue_work(uv_default_loop(), work_request, requests_queue_runner, end_work_callback);

    uv_work_t *work_sender = (uv_work_t*)calloc(1, sizeof(uv_work_t));
    uv_queue_work(uv_default_loop(), work_sender, sender_queue_runner, end_work_callback);

	uv_read_start(tcp, alloc_buffer, on_response_callback);
}

int main(int argc, char *argv[]) {
    int c;

  json_input_file = "./resources/requests.json";
  fbs_request_file = "./resources/request.fbs";
  fbs_reply_file = "./resources/reply.fbs";
  
  for(c = 0; c < argc; ++c) {
    if (argv[c][0] == '-') {
      int parse_error = 0;
      if (!strcmp(argv[c], "--json_commands") && c + 1 < argc) {
        argv[c] = NULL;
        json_input_file = argv[++c];
      } 
      else if (!strcmp(argv[c], "--fbs_request") && c + 1 < argc) {
        argv[c] = NULL;
        fbs_request_file = argv[++c];
      }
      else if (!strcmp(argv[c], "--fbs_reply") && c + 1 < argc) {
        argv[c] = NULL;
        fbs_reply_file = argv[++c];
      }
    }
  }

  if (fbs_request_file.empty())
  {
    Help();
    return EXIT_FAILURE;
  }

  if (fbs_reply_file.empty())
  {
    Help();
    return EXIT_FAILURE;
  }

  if (json_input_file.empty())
  {
    Help();
    return EXIT_FAILURE;
  }

    
    #ifdef SAS_DEBUG
    printf("Json command file [%s]\n", json_input_file.data());
    printf("FBS reply file [%s]\n", fbs_reply_file.data());
    printf("FBS request file [%s]\n", fbs_request_file.data());
    #endif

    uv_tcp_t* socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), socket);
    uv_tcp_keepalive(socket, 1, 5);
    sockaddr_in dest;
    uv_ip4_addr("127.0.0.1", 7000, &dest);
    uv_connect_t* connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));
    int uv_error_value;
    //do
    {
        uv_error_value =  uv_tcp_connect(connect, socket, (const struct sockaddr*)&dest, on_connect);
        if (uv_error_value)
        {    
            fprintf(stderr, "SAS %s\n", uv_strerror(uv_error_value));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    } 
    //while(uv_error_value != 0);

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    
    SAS_FREE(socket);
    SAS_FREE(connect);
    return 0;
}
