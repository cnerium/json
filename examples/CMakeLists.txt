file(GLOB EXAMPLE_SOURCES *.cpp)

foreach(example ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example} NAME_WE)

    add_executable(${example_name} ${example})

    target_link_libraries(${example_name} PRIVATE json::json)

    if (MSVC)
        target_compile_options(${example_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${example_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
