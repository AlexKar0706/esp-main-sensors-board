set(src_dirs        .)

set(include_dirs    .)

set(requires nvs_flash esp_event esp_netif esp_wifi esp_timer)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
