# Windows PowerShell and System.Drawing are OS components, not PATH dependencies.
set(DANMAX_ICON_DIR "${CMAKE_BINARY_DIR}/branding")
set(DANMAX_ICON "${DANMAX_ICON_DIR}/DanmaX.ico")
add_custom_target(danmax_icon
    BYPRODUCTS "${DANMAX_ICON}" "${DANMAX_ICON_DIR}/DanmaX.png"
    COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/generate-icon.ps1"
        -OutputDirectory "${DANMAX_ICON_DIR}"
    COMMENT "Checking DanmaX icon content"
    VERBATIM)
configure_file("${CMAKE_SOURCE_DIR}/src/app/branding.rc.in" "${DANMAX_ICON_DIR}/branding.rc" @ONLY)

function(danmax_attach_icon target)
    target_sources(${target} PRIVATE "${DANMAX_ICON_DIR}/branding.rc")
    # Source properties must be visible in the directory that creates the target.
    set_source_files_properties("${DANMAX_ICON_DIR}/branding.rc"
        TARGET_DIRECTORY ${target} PROPERTIES OBJECT_DEPENDS "${DANMAX_ICON}")
    add_dependencies(${target} danmax_icon)
endfunction()
