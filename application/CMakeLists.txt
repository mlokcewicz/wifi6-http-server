# main app target

target_sources(app PRIVATE main.c)
target_sources(app PRIVATE network_manager.c)
target_sources(app PRIVATE network_socket.c)
target_sources_ifdef(CONFIG_NET_ZPERF app PRIVATE network_perf.c)
target_sources_ifdef(CONFIG_MQTT_HELPER app PRIVATE network_mqtt.c)

target_include_directories(app PRIVATE .)

target_link_libraries(app PRIVATE
    utils 
    platform
)

zephyr_library_include_directories($<TARGET_PROPERTY:wifi_prov_core,BINARY_DIR>)

target_link_libraries(app PRIVATE wifi_prov_ble)
