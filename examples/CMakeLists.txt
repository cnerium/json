file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS *.cpp)

foreach(example ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${example} NAME_WE)

    set(example_target "cnerium_json_example_${example_name}")

    add_executable(${example_target} ${example})

    target_link_libraries(${example_target} PRIVATE cnerium::json)
    target_compile_features(${example_target} PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(${example_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${example_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
