#include "esp_err.h"
void http_client_get(char * url, char * cert_pem);
esp_err_t http_client_content_get(char * url, char * cert_pem, char * response_buffer);
char* http_client_get_image(const char *url);