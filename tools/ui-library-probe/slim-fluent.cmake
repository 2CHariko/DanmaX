# Explicit evaluation subset. Upstream control/C++ files remain unchanged.
set(upstream "${FLUENT_SOURCE}/src")
set(module_root "${upstream}/Qt6/imports/FluentUI")
set(native_sources)
foreach(name FluAccentColor FluColors FluTheme FluTools FluTextStyle FluRectangle)
    list(APPEND native_sources "${upstream}/${name}.cpp" "${upstream}/${name}.h")
endforeach()
list(APPEND native_sources "${upstream}/Def.h" "${upstream}/FluentIconDef.h" "${upstream}/stdafx.h" "${upstream}/singleton.h")
set(controls
    FluCheckBox FluClip FluComboBox FluControlBackground FluExpander
    FluFocusRectangle FluFrame FluIcon FluIconButton FluItemDelegate
    FluLoader FluMenu FluMenuItem FluScrollBar FluShadow FluText
    FluTextBox FluTextBoxBackground FluTextBoxMenu FluToggleSwitch FluTooltip)
set(qml_files)
foreach(name IN LISTS controls)
    set(path "${module_root}/Controls/${name}.qml")
    set_source_files_properties("${path}" PROPERTIES QT_RESOURCE_ALIAS "Controls/${name}.qml")
    list(APPEND qml_files "${path}")
endforeach()
set(font "${module_root}/Font/FluentIcons.ttf")
set_source_files_properties("${font}" PROPERTIES QT_RESOURCE_ALIAS "Font/FluentIcons.ttf")
qt_add_library(fluent_probe SHARED)
qt_add_qml_module(fluent_probe
    URI FluentUI VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/imports/FluentUI"
    RESOURCE_PREFIX /qt/qml
    SOURCES ${native_sources}
    QML_FILES ${qml_files}
    RESOURCES "${font}")
target_include_directories(fluent_probe PRIVATE "${upstream}")
target_link_libraries(fluent_probe PRIVATE Qt6::Quick)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/fluent-source-manifest.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:fluent_probe,SOURCES>,\n>\n")
