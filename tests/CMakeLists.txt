add_executable(rwin_proto_test proto_test.cpp)
target_link_libraries(rwin_proto_test PRIVATE rwin::proto)
add_test(NAME rwin_proto_test COMMAND rwin_proto_test)

add_executable(rwin_frame_timing_test frame_timing_test.cpp)
target_link_libraries(rwin_frame_timing_test PRIVATE rwin::logging)
add_test(NAME rwin_frame_timing_test COMMAND rwin_frame_timing_test)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_executable(rwin_win32_style_test
    win32_style_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/chrome/win32_style.cpp")
target_link_libraries(rwin_win32_style_test PRIVATE Qt6::Core)
target_include_directories(rwin_win32_style_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_win32_style_test COMMAND rwin_win32_style_test)

add_executable(rwin_pixel_mapping_test
    pixel_mapping_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/mirror/draw_picture.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/mirror/pixel_mapping.cpp")
target_link_libraries(rwin_pixel_mapping_test PRIVATE Qt6::Gui)
target_include_directories(rwin_pixel_mapping_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_pixel_mapping_test COMMAND rwin_pixel_mapping_test)

# The GPU device's export socket, spoken by the host half: its messages as
# accel-virt lays them out, and the bookkeeping of frames waited for.
add_executable(rwin_export_wire_test
    export_wire_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/export/copy_wait.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/export/export_answer.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/export/export_request.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/export/pci_address.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/export/unique_fd.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/export/window_ring.cpp")
target_link_libraries(rwin_export_wire_test PRIVATE rwin::proto)
target_include_directories(rwin_export_wire_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_export_wire_test COMMAND rwin_export_wire_test)

add_executable(rwin_unit_padding_test
    unit_padding_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/cursor/unit_padding.cpp")
target_link_libraries(rwin_unit_padding_test PRIVATE Qt6::Core)
target_include_directories(rwin_unit_padding_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_unit_padding_test COMMAND rwin_unit_padding_test)

add_executable(rwin_screen_mapping_test screen_mapping_test.cpp)
target_link_libraries(rwin_screen_mapping_test PRIVATE Qt6::Core)
target_include_directories(rwin_screen_mapping_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_screen_mapping_test COMMAND rwin_screen_mapping_test)

add_executable(rwin_key_translator_test
    key_translator_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/input/key_translator.cpp"
    "${PROJECT_SOURCE_DIR}/source/host/input/scan_code.cpp")
target_link_libraries(rwin_key_translator_test PRIVATE rwin::proto Qt6::Gui)
target_include_directories(rwin_key_translator_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_key_translator_test COMMAND rwin_key_translator_test)

add_executable(rwin_native_cursor_shape_test
    native_cursor_shape_test.cpp
    "${PROJECT_SOURCE_DIR}/source/host/cursor/native_cursor_shape.cpp")
target_link_libraries(rwin_native_cursor_shape_test PRIVATE rwin::proto Qt6::Core)
target_include_directories(rwin_native_cursor_shape_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_native_cursor_shape_test COMMAND rwin_native_cursor_shape_test)

# The guest half's cursor decoding that needs no Windows, built here.
add_executable(rwin_monochrome_picture_test
    monochrome_picture_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/cursor/monochrome_picture.cpp")
target_include_directories(rwin_monochrome_picture_test PRIVATE "${RWIN_INCLUDE_DIR}")
target_compile_features(rwin_monochrome_picture_test PRIVATE cxx_std_20)
add_test(NAME rwin_monochrome_picture_test COMMAND rwin_monochrome_picture_test)

add_executable(rwin_changed_tiles_test
    changed_tiles_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/capture/changed_tiles.cpp")
target_link_libraries(rwin_changed_tiles_test PRIVATE rwin::proto)
add_test(NAME rwin_changed_tiles_test COMMAND rwin_changed_tiles_test)

add_executable(rwin_slot_ledger_test
    slot_ledger_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/capture/slot_ledger.cpp")
target_link_libraries(rwin_slot_ledger_test PRIVATE rwin::proto)
add_test(NAME rwin_slot_ledger_test COMMAND rwin_slot_ledger_test)

add_executable(rwin_pointer_grab_rule_test pointer_grab_rule_test.cpp)
target_link_libraries(rwin_pointer_grab_rule_test PRIVATE rwin::proto)
add_test(NAME rwin_pointer_grab_rule_test COMMAND rwin_pointer_grab_rule_test)

add_executable(rwin_probe_wire_test probe_wire_test.cpp)
target_link_libraries(rwin_probe_wire_test PRIVATE rwin::proto)
add_test(NAME rwin_probe_wire_test COMMAND rwin_probe_wire_test)

# A picture drawn on the GPU from a dma-buf, against the host library: needs
# a display and the GPU Qt draws on, and judges nothing without them.
if(TARGET rwin_host)
    add_executable(rwin_mirror_lifecycle_test mirror_lifecycle_test.cpp)
    target_link_libraries(rwin_mirror_lifecycle_test PRIVATE rwin_host)
    add_test(NAME rwin_mirror_lifecycle_test COMMAND rwin_mirror_lifecycle_test)
    set_tests_properties(rwin_mirror_lifecycle_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(rwin_link_lifecycle_test link_lifecycle_test.cpp)
    target_link_libraries(rwin_link_lifecycle_test PRIVATE rwin_host)
    add_test(NAME rwin_link_lifecycle_test COMMAND rwin_link_lifecycle_test)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GBM IMPORTED_TARGET gbm)
    if(GBM_FOUND)
        add_executable(rwin_gpu_picture_test gpu_picture_test.cpp)
        target_link_libraries(rwin_gpu_picture_test PRIVATE rwin_host PkgConfig::GBM)
        add_test(NAME rwin_gpu_picture_test COMMAND rwin_gpu_picture_test)
    endif()
endif()

# The harness's own logic, against the library the harness is built from.
if(TARGET rwin_conform)
    foreach(name pixel_comparison expectation input_expectation scenario_book scenario_matrix pointer_expectation)
        add_executable(rwin_${name}_test ${name}_test.cpp)
        target_link_libraries(rwin_${name}_test PRIVATE rwin_conform)
        add_test(NAME rwin_${name}_test COMMAND rwin_${name}_test)
    endforeach()
    target_compile_definitions(rwin_scenario_book_test PRIVATE
        RWIN_CONFORM_SCENARIOS="${PROJECT_SOURCE_DIR}/tests/conformance/scenarios.json")
endif()
