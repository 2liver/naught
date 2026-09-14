cmake_minimum_required(VERSION 3.16)
project(naught VERSION 0.3.10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(naught WIN32 MACOSX_BUNDLE main.cpp resources/resources.qrc)
target_link_libraries(naught PRIVATE Qt6::Widgets)

# 语法高亮（可选）：未找到时 编 模式退化为 等宽字体 + 行号
find_package(KF6SyntaxHighlighting QUIET)
if(KF6SyntaxHighlighting_FOUND)
    target_link_libraries(naught PRIVATE KF6::SyntaxHighlighting)
    target_compile_definitions(naught PRIVATE NAUGHT_WITH_HIGHLIGHT)
endif()

if(MSVC)
    target_compile_options(naught PRIVATE /utf-8)
endif()

if(WIN32)
    target_sources(naught PRIVATE naught.rc)
endif()

if(APPLE)
    set_target_properties(naught PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "無"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.2liver.naught"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "naught.icns"
    )
    target_sources(naught PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/naught.icns")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/resources/naught.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# 安装规则：macOS 执行 cmake --install build --prefix ~/Applications 即可进入启动台；
# Windows/Linux 拿到可执行文件后自行放置快捷方式。
install(TARGETS naught
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

# 图标重生成工具（需要思源宋体 Heavy 的 OTF，见 README）
option(NAUGHT_ICON_TOOL "Build the icon regeneration tool" ON)
if(NAUGHT_ICON_TOOL)
    qt_add_executable(naught_icon_tool tools/render_icon.cpp)
    set_target_properties(naught_icon_tool PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(naught_icon_tool PRIVATE Qt6::Gui)
endif()
