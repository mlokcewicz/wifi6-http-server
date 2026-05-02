# main app target

target_sources(app PRIVATE main.c)
target_sources(app PRIVATE network_manager.c)
target_sources(app PRIVATE network_socket.c)
target_sources_ifdef(CONFIG_NET_ZPERF app PRIVATE network_perf.c)
target_sources_ifdef(CONFIG_MQTT_HELPER app PRIVATE network_mqtt.c)
target_sources(app PRIVATE network_http.c)
target_sources(app PRIVATE pages/index.c)

target_include_directories(app PRIVATE .)

zephyr_library_include_directories_ifdef(CONFIG_BT_WIFI_PROV $<TARGET_PROPERTY:wifi_prov_core,BINARY_DIR>)
target_link_libraries_ifdef(CONFIG_BT_WIFI_PROV app PRIVATE wifi_prov_ble)

target_link_libraries(app PRIVATE
    utils 
    platform
)


set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)

zephyr_include_directories(${gen_dir})

generate_inc_file_for_target(app credentials/AmazonRootCA1.pem ${gen_dir}/AmazonRootCA1.pem.inc)