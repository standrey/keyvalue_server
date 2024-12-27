#pragma once


/*#ifdef __cplusplus*/
/*extern "C" {*/
/*#endif*/
/**/
bool getFbValues(char * dataptr, char ** out_operation, char ** out_key, char ** out_value);
bool setFbValues(int error_code, const char * str_data, char ** out_buffer, unsigned int * out_sz);

/*#ifdef __cplusplus*/
/*}*/
/*#endif*/

