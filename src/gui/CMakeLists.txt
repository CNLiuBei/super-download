# src/gui/CMakeLists.txt
# Executable: download_manager_gui

set(CMAKE_AUTOMOC ON)

# Windows application icon resource
set(APP_RC "")
if(WIN32)
    set(APP_RC app.rc)
endif()

add_executable(super_download WIN32
    ../main.cpp
    task_model.h
    task_model.cpp
    main_window.h
    main_window.cpp
    new_download_dialog.h
    new_download_dialog.cpp
    settings_dialog.h
    settings_dialog.cpp
    clipboard_monitor.h
    clipboard_monitor.cpp
    ws_server.h
    ws_server.cpp
    ${APP_RC}
)

# Copy logo.png next to the executable for runtime icon loading
add_custom_command(TARGET super_download POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/logo.png"
        "$<TARGET_FILE_DIR:super_download>/logo.png"
    COMMENT "Copying logo.png to output directory"
)

target_link_libraries(super_download PRIVATE
    download_core
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(super_download PRIVATE ole32 uuid shell32)
    # Ensure console window is hidden (WIN32 subsystem + correct entry point)
    target_link_options(super_download PRIVATE -mwindows)
endif()
