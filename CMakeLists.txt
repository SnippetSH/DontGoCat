cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "최소 macOS 버전 (Windows 에서는 무시)")   # project() 보다 먼저
project(DontGoCat VERSION 1.4.1 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)   # macOS 네이티브 계층 (.mm)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)     # IntelliSense용 (선택)

if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "B:/Qt/6.11.2/msvc2022_64")
elseif(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.2/macos")
endif()

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Core Gui Widgets Network)

if(MSVC)
    add_compile_options(/utf-8)   # 소스의 한글 문자열
endif()

set(CCAT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/include)

# ── WP1: 스프라이트 / 애니메이션 ─────────────────────────────
add_library(ccat_sprite STATIC
    src/include/CatSprite.hpp
    src/module/CatSprite.cpp
    src/module/CatHangFrames.inc
    src/include/CatAnim.hpp
    src/module/CatAnim.cpp
)
target_include_directories(ccat_sprite PUBLIC ${CCAT_INCLUDE_DIR})
target_link_libraries(ccat_sprite PUBLIC Qt6::Gui)

add_library(ccat_preview_ui STATIC
    src/include/CatWidget.hpp
    src/module/CatWidget.cpp
)
target_link_libraries(ccat_preview_ui PUBLIC ccat_sprite Qt6::Widgets)

# ── WP2: 데스크탑 면 / 마우스 (Win32 / macOS) ─────────────────
add_library(ccat_platform STATIC
    src/include/Config.hpp
    src/include/Surface.hpp
    src/module/Surface.cpp
    src/include/DesktopScanner.hpp
    src/include/MouseWatcher.hpp
    src/module/MouseWatcher.cpp
)
target_link_libraries(ccat_platform PUBLIC ccat_sprite Qt6::Core Qt6::Gui)
if(WIN32)
    target_sources(ccat_platform PRIVATE src/module/DesktopScanner.cpp)
    target_link_libraries(ccat_platform PUBLIC user32 dwmapi shell32)
elseif(APPLE)
    # MacNative: NSWindow 설정 / 커서 / 소리 등 Obj-C++ 보조 (MouseWatcher, CatOverlay, SpeechBubble, CatVoice 가 쓴다)
    target_sources(ccat_platform PRIVATE
        src/include/MacNative.hpp
        src/module/MacNative.mm
        src/module/DesktopScanner_mac.mm
    )
    target_compile_options(ccat_platform PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(ccat_platform PUBLIC
        "-framework AppKit" "-framework CoreGraphics" "-framework ApplicationServices")
endif()

# ── WP3: 오버레이 / 트레이 / 종료 방해 ────────────────────────
add_library(ccat_ui STATIC
    src/include/AppSettingsFile.hpp
    src/module/AppSettingsFile.cpp
    src/include/AutoStart.hpp
    src/include/CatStats.hpp
    src/module/CatStats.cpp
    src/include/CatOverlay.hpp
    src/module/CatOverlay.cpp
    src/include/DpiSizeLock.hpp
    src/module/DpiSizeLock.cpp
    src/include/FileLogger.hpp
    src/module/FileLogger.cpp
    src/include/QuitGuard.hpp
    src/module/QuitGuard.cpp
    src/include/QuitPopup.hpp
    src/module/QuitPopup.cpp
    src/include/SpeechBubble.hpp
    src/module/SpeechBubble.cpp
    src/include/TrayController.hpp
    src/module/TrayController.cpp
)
target_link_libraries(ccat_ui PUBLIC ccat_platform Qt6::Widgets)
target_compile_definitions(ccat_ui PRIVATE CCAT_VERSION="${PROJECT_VERSION}")   # QuitPopup 버전 표시
if(WIN32)
    target_sources(ccat_ui PRIVATE src/module/AutoStart.cpp)
    target_link_libraries(ccat_ui PUBLIC advapi32)   # AutoStart (레지스트리)
elseif(APPLE)
    target_sources(ccat_ui PRIVATE src/module/AutoStart_mac.cpp)   # AutoStart (~/Library/LaunchAgents)
endif()

# ── 업데이트 확인 (README 5.15): 플랫폼 공통, Qt Network ──────
add_library(ccat_update STATIC
    src/include/UpdateChecker.hpp
    src/module/UpdateChecker.cpp
    src/include/UpdateNotifier.hpp
    src/module/UpdateNotifier.cpp
)
target_include_directories(ccat_update PUBLIC ${CCAT_INCLUDE_DIR})
target_link_libraries(ccat_update PUBLIC Qt6::Network Qt6::Widgets)

# ── WP4: 이동 / 경로 / 행동 / 앱 ──────────────────────────────
add_library(ccat_core STATIC
    src/include/Locomotion.hpp
    src/module/Locomotion.cpp
    src/include/PathPlanner.hpp
    src/module/PathPlanner.cpp
    src/include/CatBrain.hpp
    src/module/CatBrain.cpp
    src/include/CatVoice.hpp
    src/module/CatVoice.cpp
    src/include/CatApp.hpp
    src/module/CatApp.cpp
    src/include/BubbleUpdateNotifier.hpp
    src/module/BubbleUpdateNotifier.cpp
)
target_link_libraries(ccat_core PUBLIC ccat_ui ccat_platform ccat_sprite ccat_update)
target_compile_definitions(ccat_core PRIVATE CCAT_VERSION="${PROJECT_VERSION}")   # CatApp → UpdateChecker 현재 버전
if(WIN32)
    target_link_libraries(ccat_core PUBLIC winmm)   # CatVoice (PlaySound)
endif()

# ── 실행 파일 ─────────────────────────────────────────────────
# ccat        : 데스크탑 상주 앱 (콘솔 창 없음). 산출물 파일명은 DontGoCat.exe (타깃 이름은 내부용 ccat 유지)
# ccat_preview: 애니메이션 미리보기 / --export 시트 출력
add_executable(ccat WIN32 src/app/main.cpp resources/ccat.qrc)
target_link_libraries(ccat PRIVATE ccat_core)
set_target_properties(ccat PROPERTIES OUTPUT_NAME DontGoCat)   # 배포 이름: DontGoCat.exe
target_compile_definitions(ccat PRIVATE CCAT_VERSION="${PROJECT_VERSION}")   # 로그 첫 줄 버전

if(WIN32)
    # exe 버전 리소스 + 앱 아이콘 (ccat 타깃에만). 버전은 project(DontGoCat VERSION ...) 한 곳에서 온다
    enable_language(RC)
    set(CCAT_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/ccat.ico")
    set(CCAT_VERSION_DOT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
    set(CCAT_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    configure_file(resources/ccat.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ccat.rc" @ONLY)
    target_sources(ccat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ccat.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/ccat.rc" PROPERTIES OBJECT_DEPENDS "${CCAT_ICON_PATH}")
    # 패키징 스크립트가 읽는 버전 파일 (인스톨러 /DAppVersion 용)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/ccat_version.txt" CONTENT "${PROJECT_VERSION}")
elseif(APPLE)
    # DontGoCat.app 번들. LSUIElement = YES (Dock / 앱 전환기 미표시), 버전은 project() 에서
    set_target_properties(ccat PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.dontgocat.app"
        MACOSX_BUNDLE_BUNDLE_NAME "DontGoCat"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

add_executable(ccat_preview src/tools/preview_main.cpp)
target_link_libraries(ccat_preview PRIVATE ccat_preview_ui)

if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" REQUIRED)

    foreach(TARGET_NAME ccat ccat_preview)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    $<IF:$<CONFIG:Debug>,--debug,--release>
                    --no-translations
                    --no-compiler-runtime
                    "$<TARGET_FILE:${TARGET_NAME}>"
            COMMENT "Deploying Qt runtime for ${TARGET_NAME}"
        )
    endforeach()
endif()

if(WIN32)
    # 스캔 결과 덤프 (창 제목 / 클래스 이름은 Win32 로 읽는다)
    add_executable(ccat_scan_dump src/tools/scan_dump.cpp)
    target_link_libraries(ccat_scan_dump PRIVATE ccat_platform)
endif()

# ── 테스트 (ctest) ────────────────────────────────────────────
option(CCAT_BUILD_TESTS "Build CCat tests" ON)
enable_testing()
if(CCAT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
