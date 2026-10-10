# CCat 테스트. 각 실행 파일은 자체 경량 CHECK 프레임워크(TestCheck.hpp)를 쓰며 실패 시 종료 코드 1.
# 합성 스냅샷 + 가상 시간만 사용하고 실제 데스크탑 / 커서 / 트레이는 건드리지 않는다.

# ctest 가 Qt DLL 을 찾도록 Qt bin 디렉터리를 PATH 에 넣는다 (Qt6 패키지의 qmake 위치에서 유도)
get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)

function(ccat_add_test NAME SOURCE)
    cmake_parse_arguments(ARG "GUI" "" "LIBS" ${ARGN})
    add_executable(${NAME} ${SOURCE} TestCheck.hpp TestSnapshot.hpp)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${NAME} PRIVATE ${ARG_LIBS})
    # 시간 측정 검사는 Debug 에서 더 느슨한 상한을 쓴다
    target_compile_definitions(${NAME} PRIVATE CCAT_TEST_DEBUG=$<CONFIG:Debug>)
    add_test(NAME ${NAME} COMMAND ${NAME})

    # 기존 PATH 는 유지하고 Qt bin 만 앞에 붙인다 (ENVIRONMENT_MODIFICATION, CMake 3.22+)
    set(_mods "PATH=path_list_prepend:${_qt_bin_dir}")
    if(ARG_GUI)
        # 위젯을 만드는 테스트는 실제 화면 대신 offscreen 플랫폼
        list(APPEND _mods "QT_QPA_PLATFORM=set:offscreen")
    endif()
    set_tests_properties(${NAME} PROPERTIES ENVIRONMENT_MODIFICATION "${_mods}" TIMEOUT 300)
endfunction()

# Locomotion + PathPlanner (면 이동, 점프, 모서리, 낙하, 경로 탐색). `--real-desktop` 은 수동 전용
ccat_add_test(test_motion test_motion.cpp LIBS ccat_core)

# CatBrain 행동 (자율 soak, 트레이 접근, 종료 방해, 마우스 장난, 숨김/낙하, 재계획)
ccat_add_test(test_brain test_brain.cpp LIBS ccat_core)

# CatVoice 울음 판정 (간격 / 확률 / 끄기 / 자는 중). 실제 소리는 내지 않는다
ccat_add_test(test_voice test_voice.cpp LIBS ccat_core)

# QuitPopup 크기 / 배치 (배율별)
ccat_add_test(test_popup test_popup.cpp GUI LIBS ccat_ui)

# AutoStart (HKCU\Software\CCatTest\Run 임시 키만 사용, 실제 Run 키는 건드리지 않음)
ccat_add_test(test_autostart test_autostart.cpp LIBS ccat_ui)
