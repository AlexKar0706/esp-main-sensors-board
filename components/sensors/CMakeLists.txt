set(src_dirs        
    ./src
    ./src/vl53l1x_lib/core
    ./src/vl53l1x_lib/platform
)

set(include_dirs    
    ./inc
    ./inc/vl53l1x_lib/core
    ./inc/vl53l1x_lib/platform
)

set(requires esp_timer)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
