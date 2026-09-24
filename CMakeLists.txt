cmake_minimum_required(VERSION 3.16)
project(naught VERSION 0.3.16 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets ShaderTools)

qt_add_executable(naught WIN32 MACOSX_BUNDLE
    main.mm
    crt_view.cpp
    line_number_area.cpp
    canvas.h
    crt.cpp
    crt.h
    crt_view.h
    editor.h
    snapshot_compositor.cpp
    selftest.cpp
    snapshot_compositor.h
    line_number_area.h
    zen_scroll_bar.h
    resources/resources.qrc)
qt_add_shaders(naught "shaders"
    PREFIX "/"
    GLSL "310es,330,450"
    HLSL "50"
    MSL "12"
    FILES
        shaders/crt.vert
        shaders/crt.frag
        shaders/persist.frag
        shaders/downsample.frag
        shaders/blurH.frag
        shaders/blurV.frag
)
target_link_libraries(naught PRIVATE Qt6::Widgets)
if(NOT APPLE)
    # main.mm 的 ObjC 段全在 Q_OS_MACOS 守卫内——非 Apple 按 C++ 编译
    set_source_files_properties(main.mm PROPERTIES LANGUAGE CXX)
endif()
# Qt 6.9 的 QRhi 公共头（rhi/qrhi.h 等）只存在于版本化目录，且官方
# 打包缺 rhi 的平铺转发头——代码用 <rhi/...>，这里把版本化目录补进
# include path（三平台同一套路径规则，Qt6::Gui 的 usage requirement 不含它）
if(APPLE)
    target_include_directories(naught PRIVATE
        "${QT6_INSTALL_PREFIX}/lib/QtGui.framework/Versions/A/Headers/${Qt6_VERSION}/QtGui")
else()
    target_include_directories(naught PRIVATE
        "${QT6_INSTALL_PREFIX}/include/QtGui/${Qt6_VERSION}/QtGui")
endif()
if(APPLE)
    # M6：全局重生热键（RegisterEventHotKey）与 LaunchServices（LSOpen）
    target_link_libraries(naught PRIVATE "-framework Carbon" "-framework CoreServices")
endif()

# M6 重生代理：纯 Cocoa 登录项应用（macOS 专属）——launchd 裸进程
# 收不到系统热键投递，必须作为真正的登录项应用运行
if(APPLE)
add_executable(naught-agent naught_agent.mm)
target_link_libraries(naught-agent PRIVATE
    "-framework Cocoa" "-framework Carbon" "-framework CoreServices")
add_custom_command(TARGET naught-agent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_BINARY_DIR}/naught-agent.app/Contents/MacOS
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:naught-agent>
            ${CMAKE_BINARY_DIR}/naught-agent.app/Contents/MacOS/naught-agent
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/packaging/naught-agent-Info.plist
            ${CMAKE_BINARY_DIR}/naught-agent.app/Contents/Info.plist
    COMMAND codesign --force --sign -
            ${CMAKE_BINARY_DIR}/naught-agent.app)
endif()

# 主应用打包时把代理应用嵌进 Resources（安装程序从主包里拷贝布署）。
# 直接组装嵌套 bundle：不再 copy_directory 依赖 naught-agent 目标自身的
# POST_BUILD 时序（agent 未重建时其 bundle 可能缺失 → 构建断裂）
if(APPLE)
add_dependencies(naught naught-agent)
add_custom_command(TARGET naught POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:naught>/Resources/naught-agent.app/Contents/MacOS
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:naught-agent>
            $<TARGET_BUNDLE_CONTENT_DIR:naught>/Resources/naught-agent.app/Contents/MacOS/naught-agent
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/packaging/naught-agent-Info.plist
            $<TARGET_BUNDLE_CONTENT_DIR:naught>/Resources/naught-agent.app/Contents/Info.plist
    COMMAND codesign --force --sign -
            $<TARGET_BUNDLE_CONTENT_DIR:naught>/Resources/naught-agent.app)
endif()

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
