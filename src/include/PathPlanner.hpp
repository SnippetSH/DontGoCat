#pragma once

#include "Locomotion.hpp"
#include "Surface.hpp"

#include <QRandomGenerator>

#include <optional>
#include <vector>

// 면 그래프 위 경로 탐색 (README 5.8).
// 노드: 세그먼트 양 끝점 + kPlannerSampleSpritePx × scale 간격 샘플 (+ 시작/목표/모서리 접점/낙하 착지점)
// 간선: 같은 세그먼트 걷기 / 모서리 전환 / 점프(한계 이내) / 바닥 끝에서 걸어 나가 낙하
// 비용: 예상 소요 시간, Dijkstra
//
// 한 세그먼트 안에서는 모든 노드가 걷기로 서로 이어진다 (암묵 간선). 점프 도착점은 대상 세그먼트의 가장 가까운 샘플 노드로
// 근사하므로 최대 반 샘플 간격 정도 어긋날 수 있다 (한계 검사는 실제 노드 위치로 한다).
// 점프 중 장애물(창)은 무시한다 (고양이 창이 최상위).
struct RouteStep
{
    enum class Kind { Move, Corner, Jump, WalkOff };
    Kind kind = Kind::Move;
    SurfacePoint target;          // Move/Corner/Jump: 도착점, WalkOff: 낙하 후 예상 착지점
    bool run = false;             // Move: 달리기 여부 (벽에서는 무시, climb)
    bool towardPositive = false;  // WalkOff: 진행 방향
};

using Route = std::vector<RouteStep>;

class PathPlanner
{
public:
    void setScale(int scale);

    // from → to 경로. 이미 같은 위치면 빈 Route. 갈 수 없으면 nullopt.
    // Route 의 각 step 은 Locomotion 이 순서대로 실행한다 (applyRouteStep 참고).
    std::optional<Route> plan(const DesktopSnapshot &snapshot,
                              const SurfacePoint &from,
                              const SurfacePoint &to,
                              bool preferRun) const;

    // desktopPoint 에 가장 가까운, from 에서 도달 가능한 면 위 점.
    // desktopPoint 가 바닥 위(바로 아래 첫 바닥이 도달 가능)면 그 바닥을 우선한다.
    std::optional<SurfacePoint> nearestReachable(const DesktopSnapshot &snapshot,
                                                 const SurfacePoint &from,
                                                 QPoint desktopPoint) const;

    // from 에서 도달 가능한 무작위 지점 (탐험용, 세그먼트 길이에 비례해 뽑음)
    std::optional<SurfacePoint> randomReachable(const DesktopSnapshot &snapshot,
                                                const SurfacePoint &from,
                                                QRandomGenerator &rng) const;

private:
    int m_scale = 3;
};

// RouteStep 하나를 Locomotion 명령으로 실행한다 (Move → moveAlong, Corner → corner, Jump → jumpTo, WalkOff → walkOffEdge).
// 호출 후 loco.isBusy() 가 false 가 되면 다음 step 을 실행하면 된다.
// runFrameScale: Move(run) 의 Run 프레임 시간 배율 (기본 1.0 = 보통 달리기).
void applyRouteStep(Locomotion &loco, const RouteStep &step, double runFrameScale = 1.0);
