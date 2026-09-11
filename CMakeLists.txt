cmake_minimum_required(VERSION 3.11)

project(EcosDeNeon)

add_executable(${PROJECT_NAME}
    main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspctrl
    pspgu
    pspgum
)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    TITLE "ECOS DE NEON"
    VERSION 01.00
    BUILD_PRX 1
)
