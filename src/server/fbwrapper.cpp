#include <fmt/core.h>
#include <iostream>

#include "request_generated.h"
#include "reply_generated.h"
#include "transport.h"
#include "error_codes.h"

    bool getFbValues(transport_data * dataptr, char ** out_operation, char ** out_key, char ** out_value) {
    
        const auto request = Homework::GetRequest(dataptr->r_buffer);
        if (!request) {
            std::cerr<<"received flatbuffer is empty or null\n";
            return false;
        }

        // Move to c ++total_req;
        std::strcpy(*out_operation, request->operation()->c_str());
        out_key = nullptr;
        out_value = nullptr;

        if (request->member()) {
            std::strcpy(*out_value, request->member()->value()->c_str());
            std::strcpy(*out_key, request->member()->key()->c_str());  
            std::cout<<fmt::format("process_command [{}] {}->{}\n", out_operation, out_key, out_value);
        } else {
            std::cout<<fmt::format("process_command [{}]\n", out_operation);
        }

        return true;
    }

    bool setFbValues(ErrorCodes error_code, const char * str_data, char ** out_buffer, size_t * out_sz) {
        flatbuffers::FlatBufferBuilder builder(512);

        auto realdata = builder.CreateString(str_data);
        auto reply = Homework::CreateReply(builder, (int32_t)error_code, realdata);
        builder.Finish(reply);

        *out_buffer = (char *)builder.GetBufferPointer();
        *out_sz = builder.GetSize();

        builder.Clear();

        return true;
    }
