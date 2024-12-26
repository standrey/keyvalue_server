#pragma once

#include "transport.h"
#include "error_codes.h"

extern "C" bool getFbValues(transport_data * dataptr, char * & out_operation, char * & out_key, char * & out_value);
extern "C" bool setFbValues(ErrorCodes error_code, const char * str_data, char * & out_buffer, size_t & out_sz);


