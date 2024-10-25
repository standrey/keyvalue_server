// cpp
#include <iostream>
#include <string>
#include <list>
#include <algorithm>

// flatbuffer
#include "reply_generated.h"
#include "request_generated.h"
#include <flatbuffers/util.h>
#include <flatbuffers/idl.h>

#include "error_codes.h"
#include "transport.h"

uv_async_t async_handle;
uv_mutex_t mutex_handle;
std::list<transport_data*> container;
flatbuffers::Parser request_parser;
flatbuffers::Parser reply_parser;

char * json_input_file = NULL;
char * fbs_request_file = NULL;
char * fbs_reply_file = NULL;

bool work_finished = false;
ssize_t reply_counter = 0;
ssize_t request_counter = 0;

static void Help(void)
{
  printf("Usage:\n\n");
  printf(" client [fbs_request] [fbs_reply] [json_commands]...\n");
  
  printf("Command file options:\n");
  printf(" -json_commands ...... json commands file\n");
  printf(" -fbs_request ........ flatbuffer request file\n");
  printf(" -fbs_reply .......... flatbuffer reply file\n");
  printf("\n");
}


void on_close_free(uv_handle_t* handle) {
    free(handle);
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
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

    printf("[Reply #%ld]\t%s\n", reply_counter++, reply->value()->c_str());

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

void on_response_callback(uv_stream_t * server, ssize_t nread, const uv_buf_t * buf) {
    if (nread < 0) {
        fprintf(stderr, "read error: %s\n", uv_strerror(nread));
	    	uv_close((uv_handle_t*)server, on_close_free);
        free(buf->base);
        return;
    } else if (nread == 0) {
        free(buf->base);
		return;
    }

	  ssize_t readed_bytes = 0;
	  transport_data * tr_data = NULL;
    
    while (tr_data = read_next_flatbuffer(server, readed_bytes, nread, buf->base)) {

#ifdef SAS_DEBUG
    printf("-SAS- Raw data size in socket : %ld", readed_bytes);
    printf("-SAS- Transport data size : %ld", tr_data->r_buffer_size);
#endif

        process_command(tr_data);
		    close_transport_data(tr_data, false);
    }
    free(buf->base);

    if (reply_counter == request_counter && work_finished) {
        uv_close((uv_handle_t*)server, NULL);
        uv_stop(uv_default_loop());
    }
}

void make_fb_command(transport_data * tr_data) {
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
    transport_buffer[0] = (message_buffer_size >> 24) & 0xFF;
    transport_buffer[1] = (message_buffer_size >> 16) & 0xFF;
    transport_buffer[2] = (message_buffer_size >> 8) & 0xFF;
    transport_buffer[3] = (message_buffer_size >> 0) & 0xFF;

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
        close_transport_data(tr_data, true);
        return;
    }
}

void main_run(uv_work_t *work_request) {
    std::string request_schemafile;
    bool ok = flatbuffers::LoadFile(fbs_request_file, false, &request_schemafile);
    if (!ok) {
        fprintf(stderr, "Failed to load fbs request file '%s'\n", fbs_request_file);
        return;
    }

    ok = request_parser.Parse(request_schemafile.c_str());
    if (!ok) {
        fprintf(stderr, "Can't parse request schema");
        return;
    }

    char * json_line = NULL;
    size_t json_line_len = 0;
    ssize_t json_readed = 0;
    FILE * in = NULL;
  
    if (json_input_file != NULL) {
        in = fopen(json_input_file, "r");
    }

    while ((json_readed = getline(&json_line, &json_line_len, in ? in : stdin)) != -1) {
        transport_data *tr_data = (transport_data*)calloc(1, sizeof(transport_data));
        tr_data->handle = (uv_stream_t *)work_request->data;
        tr_data->raw_json = (char*)calloc(1, json_line_len + 1);
        strcpy(tr_data->raw_json, json_line);

        free(json_line), json_line = NULL;
        json_line_len = 0;

        uv_mutex_lock(&mutex_handle);
        container.emplace_back(tr_data);
        uv_mutex_unlock(&mutex_handle);
        uv_async_send(&async_handle);
    }

    work_finished = true;
    fclose(in);
}

void async_cb(uv_async_t *async_handle) {
    std::list<transport_data*> local_container;
    uv_mutex_lock(&mutex_handle);
    local_container.swap(container);
    uv_mutex_unlock(&mutex_handle);

    if (local_container.size() == 0) {
        return;
    }

    for (auto iter = local_container.begin(); iter != local_container.end(); ++iter) {
        make_fb_command(*iter);
        send_fb_command(*iter);
    }
}

void end_work_callback(uv_work_t *req, int status) {
    //uv_close((uv_handle_t*)req->data, NULL);
    //uv_stop(uv_default_loop());
    free(req);
}

void on_connect(uv_connect_t * connection, int status) {
    if (status < 0) {
        fprintf(stderr, "can't connect to server: %s\n", uv_err_name(status));
        free(connection);
        return;
    }

    uv_stream_t * tcp = (uv_stream_t *)connection->handle;
    free(connection);
	
	uv_read_start(tcp, alloc_buffer, on_response_callback);
	
    uv_work_t * work_request = (uv_work_t *)calloc(1, sizeof(uv_work_t));
    work_request->data = tcp;
    uv_queue_work(uv_default_loop(), work_request, main_run, end_work_callback);
}

int main(int argc, char **argv) {
    int c;
  int ok;

  for(c = 0; ok && c < argc; ++c)
  {
    if (argv[c][0] == '-')
    {
      int parse_error = 0;
      if (!strcmp(argv[c], "-json_commands") && c + 1 < argc)
      {
        argv[c] = NULL;
        json_input_file = argv[++c];
      } 
      else if (!strcmp(argv[c], "-fbs_request") && c + 1 < argc)
      {
        argv[c] = NULL;
        fbs_request_file = argv[++c];
      }
      else if (!strcmp(argv[c], "-fbs_reply") && c + 1 < argc)
      {
        argv[c] = NULL;
        fbs_reply_file = argv[++c];
      }
    }
  }

  if (fbs_request_file == NULL || strlen(fbs_request_file) ==0)
  {
    Help();
    return EXIT_FAILURE;
  }

  if (fbs_reply_file == NULL || strlen(fbs_reply_file) == 0)
  {
    Help();
    return EXIT_FAILURE;
  }

  if (json_input_file == NULL || strlen(json_input_file) == 0)
  {
    Help();
    return EXIT_FAILURE;
  }

    uv_tcp_t* socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), socket);
    uv_tcp_keepalive(socket, 1, 5);
    sockaddr_in dest;
    uv_ip4_addr("127.0.0.1", 7000, &dest);
    uv_connect_t* connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));
    uv_tcp_connect(connect, socket, (const struct sockaddr*)&dest, on_connect);
    uv_async_init(uv_default_loop(), &async_handle, async_cb);
    uv_mutex_init(&mutex_handle);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    uv_close((uv_handle_t*)&async_handle, NULL);
    uv_mutex_destroy(&mutex_handle);
    free(socket);

    return 0;
}
