file(GLOB SOURCES "src/*.c")
file(GLOB SSD "ssd1306/*.c")

idf_component_register(
    SRCS ${SOURCES} ${SSD}
    INCLUDE_DIRS "lib/" "ssd1306/"
    REQUIRES esp_timer esp_wifi esp_http_client esp-tls driver nvs_flash
)