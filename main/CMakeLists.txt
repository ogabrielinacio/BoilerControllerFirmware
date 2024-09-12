idf_component_register(SRCS "app_main.c" 
                    "../libs/wifi_prov_ble/wifi_prov_ble.c"
                    "../libs/mqtt_tcp/mqtt_tcp.c"
                    INCLUDE_DIRS "."  
                        "../libs/wifi_prov_ble/include"
                        "../libs/mqtt_tcp/include"
                        )


if(DEFINED ENV{MQTT_URL})
    set(MQTT_URL $ENV{MQTT_URL})
endif()

if(DEFINED ENV{MQTT_USER})
    set(MQTT_USER $ENV{MQTT_USER})
endif()

if(DEFINED ENV{MQTT_PASS})
    set(MQTT_PASSWD $ENV{MQTT_PASSWD})
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    MQTT_URL="${MQTT_URL}"
    MQTT_USER="${MQTT_USER}"
    MQTT_PASSWD="${MQTT_PASSWD}"
)