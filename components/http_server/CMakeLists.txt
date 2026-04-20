set(src_dirs        .)

set(include_dirs    .)

set(requires esp-tls nvs_flash esp_netif esp_http_server esp_timer)
idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs)
else()
    list(APPEND requires esp_wifi esp_eth)
endif()

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})