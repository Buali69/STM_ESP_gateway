idf_component_register(
    SRCS
        "src/stm_fw_storage.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES
        littlefs
    )