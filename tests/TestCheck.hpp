#pragma once

// 테스트 공용 체크 프레임워크 (외부 의존성 없음).
// CHECK(cond, printf 형식...) 로 검사하고, main 끝에서 testResult() 로 요약 출력 + 종료 코드를 얻는다.

#include <cstdio>

namespace testing {
inline int g_pass = 0;
inline int g_fail = 0;

// 모든 검사가 끝난 뒤 호출. 실패가 하나라도 있으면 1
inline int testResult(const char *name)
{
    std::printf("\n%s: %d passed, %d failed\n", name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
} // namespace testing

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++testing::g_fail;                                                 \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        } else {                                                               \
            ++testing::g_pass;                                                 \
        }                                                                      \
    } while (0)
