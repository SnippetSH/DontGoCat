#pragma once

#include "CatSprite.hpp"

#include <vector>

// Climb 이후는 데스크탑 상주용 추가 동작 (README 5.1)
//   Climb : 벽타기 (기본 프레임에서 그리고 renderOriented로 회전)
//   Mantle: 벽 꼭대기 → 창 지붕으로 넘어가기 (1회성)
//   Roll  : 뒹굴기 / Sprawl: 길게 엎드려 버튼 덮기 / Crouch: 덮치기 준비 실룩
//   Hang  : 커서에 앞발로 매달려 대롱대롱 (잡기, README 5.13). 그립 점(CatSprite::hangGripIn) 고정, Down 으로만 그린다
enum class CatAnim {
    Idle, Walk, Run, Sit, Sleep, JumpUp, Fall, Land, PawSwipe,
    Climb, Mantle, Roll, Sprawl, Crouch, Hang,
    Count
};

struct CatAnimInfo
{
    const char *name;
    std::vector<int> frameMs;   // 프레임별 표시 시간
    bool loop;

    // 이동 애니메이션 메타데이터 (키포즈의 디딤발에서 계산).
    // 매 프레임 진행 방향으로 speedPxPerFrame만큼 옮기면 디딤발이 지면에 고정된다.
    int speedPxPerFrame = 0;
    int cycleDistancePx = 0;    // 1주기 이동 거리 = speed * 프레임 수

    int frameCount() const { return int(frameMs.size()); }
    int totalMs() const;
};

const CatAnimInfo &animInfo(CatAnim anim);

// 프레임 i의 키포즈. Pixel 모드 재생은 이것을 그대로 쓴다 (프레임 사이 보간 없음 → 지글거림 없음)
CatPose keyPose(CatAnim anim, int frame);

// t: 1주기 정규화 시간 [0, 1). 프레임 길이를 반영해 키포즈 사이를 lerp 보간
CatPose poseFor(CatAnim anim, float t);

// 프레임 i가 시작되는 정규화 시간
float frameStartT(CatAnim anim, int frame);

// 애니메이션 전 프레임(기본 프레임, 왼쪽 보기)의 불투명 영역 (스프라이트 px).
// intersection=false: 모든 프레임 불투명 픽셀 합집합의 경계 사각형.
// intersection=true : 모든 프레임에서 항상 (빈틈 없이) 불투명한 사각형 중 넓이가 가장 큰 것.
//   픽셀 교집합의 경계 사각형이 아니라 "전부 채워진" 사각형이므로, 이 안에 든 버튼은 어느 프레임에서도
//   몸에 완전히 가려진다. 넓이가 같으면 폭이 넓은 것, 그다음 아래쪽 것. 없으면 null QRect.
// QuitPopup은 Sprawl의 교집합으로 종료 버튼 크기를 정한다.
// TODO(decision): 교집합을 경계 사각형 대신 최대 불투명 사각형으로 정의함 (버튼을 완전히 덮기 위해)
QRect animOpaqueBounds(CatAnim anim, bool intersection);
