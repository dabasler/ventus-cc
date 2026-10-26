option(PLUGIN_STANDARD_QVENTUS "Build VENTUS 3D wind exposure plugin" OFF)
if(PLUGIN_STANDARD_QVENTUS)
    project(QVENTUS_PLUGIN VERSION 0.2.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    AddPlugin(NAME ${PROJECT_NAME})
    # Support both installed copies (qVENTUS/core/) and development symlinks
    # (CloudCompare/.../qVENTUS -> ~/dev/ventus-cc/qVENTUS). CMake's
    # CMAKE_CURRENT_SOURCE_DIR may preserve the *symlink* path, so ../core
    # must be resolved relative to the real directory, not the CC tree.
    get_filename_component(VENTUS_REAL_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/src/Engine.cpp")
        set(VENTUS_CORE "${CMAKE_CURRENT_SOURCE_DIR}/core")
    elseif(EXISTS "${VENTUS_REAL_PLUGIN_DIR}/../core/src/Engine.cpp")
        set(VENTUS_CORE "${VENTUS_REAL_PLUGIN_DIR}/../core")
    else()
        message(FATAL_ERROR "VENTUS core/src/Engine.cpp not found. "
            "Use the vendored installer or symlink qVENTUS from the ventus-cc repository.")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE
        qVENTUS.h qVENTUS.cpp VentusDialog.h VentusDialog.cpp
        qVENTUS.qrc
        ${VENTUS_CORE}/src/Engine.cpp
        ${VENTUS_CORE}/include/ventus/Engine.h
    )
    # Compile the Qt resource containing info.json and the VENTUS icon.
    # Without AUTORCC, the ccStdPluginInterface metadata resource is missing.
    set_target_properties(${PROJECT_NAME} PROPERTIES AUTORCC ON)
    target_include_directories(${PROJECT_NAME} PRIVATE ${VENTUS_CORE}/include)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(${PROJECT_NAME} Qt6::Widgets)
endif()
