// anci c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// cpp
#include <iostream>
#include <string>
#include <list>
#include <algorithm>

// flatbuffer
#include <flatbuffers/util.h>
#include <flatbuffers/idl.h>
#include "monster_generated.h"  // Already includes "flatbuffers/flatbuffers.h".

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>

using namespace MyGame::Sample;

// libuv
#include <uv.h>

struct transport_data {
    // request data
    char *request_buffer;

    // response raw data
    char *response_buffer;
    ssize_t response_size;

    uv_write_t *write_req;
    uv_tcp_t *client;

    //

    uv_stream_t*    server;
    char*           raw_json;
    char*           transport_data;
    size_t          transport_data_size;
    uv_write_t*     write_req;
};

uv_async_t async_handle;
uv_mutex_t mutex_handle;
std::list<transport_data*> container;
flatbuffers::Parser request_parser;
flatbuffers::Parser reply_parser;
char * input_file = NULL;

void close_transport_data(transport_data *data, bool close_handle) {
    if(!data) {
        return;
    }
    if (close_handle && data->client) {
        uv_close((uv_handle_t *) data->client, on_close_free);
    }
    if (data->request_buffer) {
        free(data->request_buffer), data->request_buffer = NULL;
    }
    if (data->response_buffer) {
        free(data->response_buffer), data->response_buffer = NULL;
    }
    if(data->write_req) {
        free(data->write_req), data->write_req = NULL;
    }
    free(data), data = NULL;
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

void process_command(char *flatbuffer_binary) {
    // once initializer
    {
        static std::string reply_schemafile;
        static bool ok_read = flatbuffers::LoadFile("../../cppfbs/reply.fbs", false, &reply_schemafile);
        if (!ok_read) {
            fprintf(stderr, "Failed to load [reply.fbs]\n");
            return;
        }
        static bool ok_parse = reply_parser.Parse(reply_schemafile.c_str());
        if (!ok_parse) {
            fprintf(stderr, "Can't parse reply schema\n");
            return;
        }
    }
    std::string json_reply_str;
    if (!GenerateText(reply_parser, flatbuffer_binary, &json_reply_str)) {
        fprintf(stderr, "can't generate json text from binary buffer");
    }
    json_reply_str.erase(std::remove(std::begin(json_reply_str), std::end(json_reply_str), '\n'), std::end(json_reply_str));
    static ssize_t reply_counter = 0;
    ++reply_counter;
    printf("[%ld]\t%s\n", reply_counter, json_reply_str.c_str());
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
    data->request_buffer = msg.message_buffer;
    data->client = (uv_tcp_s *) stream;

    msg.message_buffer = NULL;
    msg.message_size = 0;
    msg.message_size_readed_bytes = 0;
    msg.message_tail_sz = 0;

    return data;
}

void on_response(uv_stream_t *stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread == -1) {
        fprintf(stderr, "error echo_read");
        return;
    }

    // processing
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "read error:%s\n", uv_strerror(nread));
        }
        free(buf->base);
        return;
    } else if (nread == 0) {
        free(buf->base);
    }

    while (read_next_flatbuffer()) {
        process_command(reply_buffer);
    }
    free(buf->base);
}

void on_write_end(uv_write_t *req, int status) {
    if (status == -1) {
        fprintf(stderr, "error on_write_end: %s\n", uv_err_name(status));
        return;
    }
    uv_read_start(req->handle, alloc_buffer, on_response);
}

void process_command(transport_data* data) {
    bool ok = request_parser.Parse(data->raw_json);
    if (!ok) {
        fprintf(stderr, "Can't parse JSON line \n\t %s\n", data->raw_json);
        return;
    }

    // calculate sizes
    int message_buffer_size = (int) request_parser.builder_.GetSize();
    int transport_buffer_size = sizeof(char) * message_buffer_size + sizeof(int);

    // allocate memory
    char *transport_buffer = (char *) malloc(transport_buffer_size);

    // copy int to char buffer
    transport_buffer[0] = (message_buffer_size >> 24) & 0xFF;
    transport_buffer[1] = (message_buffer_size >> 16) & 0xFF;
    transport_buffer[2] = (message_buffer_size >> 8) & 0xFF;
    transport_buffer[3] = (message_buffer_size >> 0) & 0xFF;

    // copy binary data from flatbuffer to char buffer
    memcpy(transport_buffer + sizeof(int), (void *) request_parser.builder_.GetBufferPointer(), message_buffer_size);
    data->transport_data = transport_buffer;
    data->transport_data_size = transport_buffer_size;
}

void end_callback(uv_work_t *req, int status) {
    //uv_close((uv_handle_t*)req->data, NULL);
    //uv_stop(uv_default_loop());
    free(req);
}

void send_command(transport_data *data) {
    uv_buf_t buf = uv_buf_init(data->transport_data, data->transport_data_size);
    data->write_req = (uv_write_t *) malloc(sizeof(uv_write_t));
    data->write_req->data = data;

    int buf_count = 1;
    int res = uv_write(data->write_req, data->server, &buf, buf_count, on_write_end);
    if (res) {
        fprintf(stderr, "error on_connect: %s\n", uv_err_name(res));
        close_transport_data(data, true);
        return;
    }
}

void main_run(uv_work_t *work_request) {
    std::string request_schemafile;
    bool ok = flatbuffers::LoadFile("../../cppfbs/request.fbs", false, &request_schemafile);
    if (!ok) {
        fprintf(stderr, "Failed to load [request.fbs]\n");
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
    if (input_file != NULL) {
        in = fopen(input_file, "r");
    }
    while ((json_readed = getline(&json_line, &json_line_len, in ? in : stdin)) != -1) {
        transport_data *data = (transport_data*)calloc(1, sizeof(transport_data));
        data->server = (uv_stream_t *)work_request->data;
        data->raw_json = (char*)calloc(1, json_line_len + 1);
        strcpy(data->raw_json, json_line);

        uv_mutex_lock(&mutex_handle);
        container.emplace_back(data);
        uv_mutex_unlock(&mutex_handle);
        uv_async_send(&async_handle);
    }
    fclose(in);
    free(json_line);
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
        process_command(*iter);
        send_command(*iter);
    }
}

void on_connect(uv_connect_t * connection, int status) {
    if (status < 0) {
        fprintf(stderr, "can't connect to server: %s\n", uv_err_name(status));
        free(connection);
        return;
    }

    uv_stream_t * tcp = (uv_stream_t *)connection->handle;
    free(connection);
    uv_work_t * work_request = (uv_work_t *)malloc(sizeof(uv_work_t));
    work_request->data = tcp;
    uv_queue_work(uv_default_loop(), work_request, main_run, end_callback);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        input_file = argv[1];
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

// Example how to use FlatBuffers to create and read binary buffers.

int main2(int /*argc*/, const char * /*argv*/[]) {
 
  start_client_server();

  // Build up a serialized buffer algorithmically:
  flatbuffers::FlatBufferBuilder builder;

  // First, lets serialize some weapons for the Monster: A 'sword' and an 'axe'.
  auto weapon_one_name = builder.CreateString("Sword");
  short weapon_one_damage = 3;

  auto weapon_two_name = builder.CreateString("Axe");
  short weapon_two_damage = 5;

  // Use the `CreateWeapon` shortcut to create Weapons with all fields set.
  auto sword = CreateWeapon(builder, weapon_one_name, weapon_one_damage);
  auto axe = CreateWeapon(builder, weapon_two_name, weapon_two_damage);

  // Create a FlatBuffer's `vector` from the `std::vector`.
  std::vector<flatbuffers::Offset<Weapon>> weapons_vector;
  weapons_vector.push_back(sword);
  weapons_vector.push_back(axe);
  auto weapons = builder.CreateVector(weapons_vector);

  // Second, serialize the rest of the objects needed by the Monster.
  auto position = Vec3(1.0f, 2.0f, 3.0f);

  auto name = builder.CreateString("MyMonster");

  unsigned char inv_data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  auto inventory = builder.CreateVector(inv_data, 10);

  // Shortcut for creating monster with all fields set:
  auto orc = CreateMonster(builder, &position, 150, 80, name, inventory,
                           Color_Red, weapons, Equipment_Weapon, axe.Union());

  builder.Finish(orc);  // Serialize the root of the object.

  // We now have a FlatBuffer we can store on disk or send over a network.

  // ** file/network code goes here :) **
  // access builder.GetBufferPointer() for builder.GetSize() bytes

  // Instead, we're going to access it right away (as if we just received it).

  // Get access to the root:
  auto monster = GetMonster(builder.GetBufferPointer());

  // Get and test some scalar types from the FlatBuffer.
  assert(monster->hp() == 80);
  assert(monster->mana() == 150);  // default
  assert(monster->name()->str() == "MyMonster");

  // Get and test a field of the FlatBuffer's `struct`.
  auto pos = monster->pos();
  assert(pos);
  assert(pos->z() == 3.0f);
  (void)pos;

  // Get a test an element from the `inventory` FlatBuffer's `vector`.
  auto inv = monster->inventory();
  assert(inv);
  assert(inv->Get(9) == 9);
  (void)inv;

  // Get and test the `weapons` FlatBuffers's `vector`.
  std::string expected_weapon_names[] = { "Sword", "Axe" };
  short expected_weapon_damages[] = { 3, 5 };
  auto weps = monster->weapons();
  for (unsigned int i = 0; i < weps->size(); i++) {
    assert(weps->Get(i)->name()->str() == expected_weapon_names[i]);
    assert(weps->Get(i)->damage() == expected_weapon_damages[i]);
  }
  (void)expected_weapon_names;
  (void)expected_weapon_damages;

  // Get and test the `Equipment` union (`equipped` field).
  assert(monster->equipped_type() == Equipment_Weapon);
  auto equipped = static_cast<const Weapon *>(monster->equipped());
  assert(equipped->name()->str() == "Axe");
  assert(equipped->damage() == 5);
  (void)equipped;

  printf("The FlatBuffer was successfully created and verified!\n");
}

