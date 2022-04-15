#include <lwip/err.h>

void panic();

char* format_hex(uint8_t* data, size_t len);

esp_err_t lwip_err_to_esp_err(err_t lwip_err);