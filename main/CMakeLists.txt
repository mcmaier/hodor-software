file(GLOB_RECURSE HODOR_SRCS "*.c")

idf_component_register(
    SRCS        ${HODOR_SRCS}
    INCLUDE_DIRS
        "."
        "state_machine"
        "control"
        "motor"
        "sensing"
        "io"
        "comm"
        "config"
        "hal")
