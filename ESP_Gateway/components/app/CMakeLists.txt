idf_component_register(
    SRCS
        "src/tasks.cpp"

    INCLUDE_DIRS
        "include"

    PRIV_REQUIRES
        stm_fw_storage
    
    REQUIRES
        freertos
        esp_timer
        esp_system
        log
        io
        core
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FW_VERSION="SSC_1_043"
    DISABLE_LOCAL_ROLLBACK_CONFIRM_FOR_TEST=0
)