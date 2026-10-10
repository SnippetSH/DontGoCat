#include "CatAnim.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace {

constexpr int kAnimCount = int(CatAnim::Count);
constexpr int kFrames[kAnimCount] = {6, 8, 6, 4, 4, 3, 2, 2, 5,
                                    8, 3, 4, 8, 4, 4};   // climb, mantle, roll, sprawl, crouch, hang

int wrap(int i, int n) { return ((i % n) + n) % n; }

void setLeg(CatPose &p, CatLegId id, qreal dx, qreal dy)
{
    p.legs[id].foot = QPointF(dx, dy);
}

void hideLegs(CatPose &p, std::initializer_list<CatLegId> ids)
{
    for (CatLegId id : ids)
        p.legs[id].visible = false;
}

// 왼쪽 보기 좌표: 앞 = -x. 디딤발은 몸 기준 뒤(+x)로 밀린다.

CatPose idle(int f)
{
    constexpr float eye[] = {1, 1, 1, 1, 0, 1};   // 5번째 프레임에 깜빡임
    constexpr CatTail tail[] = {CatTail::Up, CatTail::Up, CatTail::Sway,
                                CatTail::Sway, CatTail::Sway, CatTail::Up};
    CatPose p;
    p.eyeOpen = eye[f];
    p.tail = tail[f];
    return p;
}

// 트롯: 대각선 쌍 A(앞 가까운 발 + 뒤 먼 발), B(앞 먼 발 + 뒤 가까운 발)가 반 주기 차이로 교대.
// 디딤 4프레임 동안 1px/프레임씩 뒤로 → 이동 속도 1px/프레임
CatPose walk(int f)
{
    constexpr int dx[] = {-2, -1, 0, 1, 1, 0, -1, -2};
    constexpr int dy[] = { 0,  0, 0, 0, -1, -1, -1, -1};
    constexpr int bob[] = {0, 0, 1, 1, 0, 0, 1, 1};   // 한 걸음마다 1px 출렁
    CatPose p;
    p.body = QPointF(0, bob[f]);
    p.head = QPointF(0, bob[wrap(f - 1, 8)]);         // 머리는 1프레임 늦게 따라옴
    const int a = f, b = wrap(f + 4, 8);
    setLeg(p, NearFront, dx[a], dy[a]);
    setLeg(p, FarBack,   dx[a], dy[a]);
    setLeg(p, FarFront,  dx[b], dy[b]);
    setLeg(p, NearBack,  dx[b], dy[b]);
    return p;
}

// 갤럽: 앞발 쌍, 뒷발 쌍이 반 주기 차이, 같은 쌍의 먼 발은 1프레임 늦음.
// 디딤 2프레임 동안 2px/프레임씩 뒤로 → 이동 속도 2px/프레임
CatPose run(int f)
{
    constexpr int dx[] = {-1, 1, 3, 1, -2, -3};
    constexpr int dy[] = { 0, 0, -1, -2, -2, -1};
    constexpr int bob[] = {0, 1, 0, 0, 0, -1};
    CatPose p;
    p.body = QPointF(0, bob[f]);
    p.head = QPointF(0, bob[wrap(f - 1, 6)]);
    p.tail = CatTail::Low;
    const auto leg = [&](CatLegId id, int k) { setLeg(p, id, dx[wrap(k, 6)], dy[wrap(k, 6)]); };
    leg(NearFront, f);
    leg(FarFront,  f + 1);
    leg(NearBack,  f + 3);
    leg(FarBack,   f + 4);
    return p;
}

CatPose sit(int f)
{
    CatPose p;
    if (f == 0) {               // 웅크리며 엉덩이를 내림
        p.body = QPointF(0, 1);
        p.head = QPointF(0, 1);
        return p;
    }
    p.bodyShape = CatBody::Sit;
    p.tail = CatTail::Ground;
    p.head = QPointF(0, f == 1 ? 0 : -1);   // 가슴을 세우며 고개가 올라감
    hideLegs(p, {NearBack, FarBack});
    return p;
}

CatPose sleep(int f)
{
    constexpr int headY[] = {5, 5, 4, 4};   // 숨쉬기보다 1프레임 늦게
    CatPose p;
    p.bodyShape = (f == 1 || f == 2) ? CatBody::LoafInhale : CatBody::Loaf;
    p.head = QPointF(0, headY[f]);
    p.tail = CatTail::Ground;
    p.eyeOpen = 0;
    hideLegs(p, {NearFront, FarFront, NearBack, FarBack});
    return p;
}

CatPose jumpUp(int f)
{
    CatPose p;
    switch (f) {
    case 0:                     // 웅크림
        p.body = QPointF(0, 1);
        p.head = QPointF(0, 1);
        break;
    case 1:                     // 뒷발로 밀어냄
        p.body = QPointF(0, -1);
        p.tail = CatTail::Low;
        setLeg(p, NearFront, -2, -2);
        setLeg(p, FarFront,  -1, -2);
        setLeg(p, NearBack,   2,  0);
        setLeg(p, FarBack,    2,  0);
        break;
    default:                    // 공중에서 쭉 뻗음
        p.body = QPointF(0, -2);
        p.head = QPointF(0, -2);
        p.tail = CatTail::Low;
        setLeg(p, NearFront, -3, -3);
        setLeg(p, FarFront,  -2, -3);
        setLeg(p, NearBack,   3, -1);
        setLeg(p, FarBack,    3, -1);
        break;
    }
    return p;
}

CatPose fall(int f)
{
    CatPose p;
    p.body = QPointF(0, -1);
    p.head = QPointF(0, -1);
    p.tail = f == 0 ? CatTail::Up : CatTail::Sway;
    setLeg(p, NearFront, -1, 0);   // 앞발을 아래로 뻗어 착지 준비
    setLeg(p, NearBack,   1, -1);
    setLeg(p, FarBack,    1, -1);
    return p;
}

CatPose land(int f)
{
    CatPose p;
    if (f == 0) {               // 충격 흡수: 몸이 눌리고 머리는 한 박자 더 내려감
        p.body = QPointF(0, 1);
        p.head = QPointF(0, 2);
        p.tail = CatTail::Low;
        setLeg(p, NearFront, -1, 0);
        setLeg(p, FarFront,  -1, 0);
        setLeg(p, NearBack,   1, 0);
        setLeg(p, FarBack,    1, 0);
    } else {
        p.head = QPointF(0, 1);
    }
    return p;
}

// 앞발 휘두르기: 들어올림 → 크게 젖힘 → (빠르게) 휘두름 → 내려침 → 복귀
CatPose pawSwipe(int f)
{
    constexpr QPointF paw[] = {{-1, -4}, {-1, -6}, {-5, -5}, {-5, -2}, {-2, 0}};
    constexpr int headY[] = {0, 0, 1, 1, 0};
    CatPose p;
    p.head = QPointF(0, headY[f]);
    p.legs[NearFront].foot = paw[f];
    p.legs[NearFront].merged = false;   // 가슴 앞에 든 발은 외곽선으로 분리
    return p;
}

// 벽타기: 길게 뻗은 몸통을 벽(= 기본 프레임의 바닥)에 바짝 붙이고, 앞발이 번갈아 멀리 뻗어 걸친다.
// 뻗은 다리는 옆으로 누운 팔처럼 그려진다. 팔 하나의 주기: 디딤 4프레임(벽에 닿음, 몸통이
// 앞으로 가므로 x가 1px씩 뒤로) → 공중 4프레임(들어서 앞으로 크게 뻗고 안쪽으로 걸침).
// 디딤발이 4프레임 연속 y=0, 1px/프레임 → 이동 속도 1px/프레임. 앞발 둘·뒷발 둘은 각각 반 주기 차이.
void climbLeg(CatPose &p, CatLegId id, int k, int x0)
{
    constexpr int sx[] = {0, 1, 2, 3, 3, 1, -1, -1};
    constexpr int sy[] = {0, 0, 0, 0, -1, -2, -2, -1};
    k = wrap(k, 8);
    setLeg(p, id, x0 + sx[k], sy[k]);
    p.legs[id].merged = k < 4;   // 들린 팔은 몸통과 외곽선으로 분리
}

CatPose climb(int f)
{
    constexpr int bob[] = {0, 0, -1, -1, 0, 0, -1, -1};   // 머리는 한 걸음마다 1px 출렁
    CatPose p;
    p.bodyShape = CatBody::Stretch;
    p.body = QPointF(0, 1);
    p.head = QPointF(-1, 6 + bob[f]);
    p.tail = CatTail::Low;
    climbLeg(p, NearFront, f,     -8);
    climbLeg(p, FarFront,  f + 4, -7);
    climbLeg(p, NearBack,  f + 2,  0);
    climbLeg(p, FarBack,   f + 6,  1);
    return p;
}

// 모서리 넘기: 앞발을 지붕에 걸치고 → 몸을 끌어올리고 → 올라섬. 뒷발은 아직 벽 모서리에 걸려 바닥선 아래로 내려감
CatPose mantle(int f)
{
    CatPose p;
    p.bodyShape = CatBody::Stretch;
    p.tail = CatTail::Low;
    setLeg(p, NearFront, -10, 0);
    setLeg(p, FarFront,   -8, 0);
    switch (f) {
    case 0:                     // 앞발을 걸침
        p.body = QPointF(0, 1);
        p.head = QPointF(-2, 7);
        setLeg(p, NearBack, 2, 1);
        setLeg(p, FarBack,  1, 1);
        break;
    case 1:                     // 끌어올리는 중: 몸이 앞으로, 뒷발이 모서리를 차고 올라옴
        p.body = QPointF(-1, 1);
        p.head = QPointF(-2, 6);
        setLeg(p, NearBack, 3, 0);
        setLeg(p, FarBack,  2, 0);
        break;
    default:                    // 올라섬
        p.body = QPointF(-2, 0);
        p.head = QPointF(-2, 5);
        setLeg(p, NearBack, 2, 0);
        setLeg(p, FarBack,  1, 0);
        break;
    }
    return p;
}

// 뒹굴기: 등을 대고 누워 배를 보이고 좌우로 흔들. 다리는 위로 허우적
CatPose roll(int f)
{
    constexpr int bx[] = {0, -1, 0, 1};
    // 앞발은 머리 쪽으로 굽히고 뒷발은 뒤로 차올리며 번갈아 허우적 (NearFront, FarFront, NearBack, FarBack)
    constexpr QPointF foot[4][CatLegCount] = {
        {{-1, 0},  {-1, -1}, {1, -1}, {1, 0}},
        {{-2, -1}, {-1, 0},  {2, 0},  {0, -1}},
        {{-1, -1}, {-2, 0},  {1, 0},  {1, -1}},
        {{0, 0},   {-1, -1}, {0, -1}, {2, 0}},
    };
    CatPose p;
    constexpr CatBody shape[] = {CatBody::Roll, CatBody::RollLeft, CatBody::Roll, CatBody::RollRight};
    p.bodyShape = shape[f];
    p.body = QPointF(bx[f], 0);
    p.head = QPointF(-2 + bx[f], f == 1 ? 7 : 8);
    p.tail = (f & 1) ? CatTail::RollB : CatTail::RollA;
    for (int i = 0; i < CatLegCount; ++i)
        p.legs[i].foot = foot[f][i];
    return p;
}

// 길게 엎드림: 앞발은 몸통 맵에 포함. 꼬리만 살랑, 6번째 프레임에 깜빡임
CatPose sprawl(int f)
{
    constexpr CatTail tail[] = {CatTail::SprawlA, CatTail::SprawlB, CatTail::SprawlC, CatTail::SprawlB,
                                CatTail::SprawlA, CatTail::SprawlB, CatTail::SprawlC, CatTail::SprawlB};
    CatPose p;
    p.bodyShape = CatBody::Sprawl;
    p.head = QPointF(5, 8);
    p.tail = tail[f];
    p.eyeOpen = f == 5 ? 0 : 1;
    hideLegs(p, {NearFront, FarFront, NearBack, FarBack});
    return p;
}

// 웅크림: 몸을 낮추고 엉덩이를 좌우로 실룩, 눈은 크게 뜸
CatPose crouch(int f)
{
    constexpr int bx[] = {-1, -2, -1, 0};
    CatPose p;
    p.bodyShape = CatBody::Crouch;
    p.body = QPointF(bx[f], 0);
    p.head = QPointF(-2, 7);
    p.tail = (f & 1) ? CatTail::CrouchB : CatTail::CrouchA;
    p.wideEyes = true;
    hideLegs(p, {NearFront, FarFront, NearBack, FarBack});
    return p;
}

// 매달림: 사용자 원화에서 만든 32x32 프레임 (CatSprite::render 가 hangFrame 번호로 그린다). 그립 점 고정, 어깨 아래만 좌우로 흔들림
CatPose hang(int f)
{
    CatPose p;
    p.bodyShape = CatBody::Hang;
    p.hangFrame = f;
    return p;
}

CatPose (*const kPoseFns[kAnimCount])(int) = {
    idle, walk, run, sit, sleep, jumpUp, fall, land, pawSwipe,
    climb, mantle, roll, sprawl, crouch, hang,
};

// 디딤발(연속 두 프레임 모두 지면 y=0)의 x 변화로 이동 속도 계산
int computeSpeed(CatAnim anim, const CatAnimInfo &info)
{
    if (!info.loop || anim == CatAnim::Roll || anim == CatAnim::Hang)   // 뒹굴거나 매달렸을 때 다리는 지면이 아니라 허공에 있다
        return 0;
    std::map<int, int> votes;
    const int n = info.frameCount();
    for (int i = 0; i < n; ++i) {
        const CatPose a = keyPose(anim, i), b = keyPose(anim, wrap(i + 1, n));
        for (int l = 0; l < CatLegCount; ++l) {
            if (!a.legs[l].visible || !b.legs[l].visible)
                continue;
            if (a.legs[l].foot.y() != 0 || b.legs[l].foot.y() != 0)
                continue;
            ++votes[CatSprite::snap(b.legs[l].foot.x() - a.legs[l].foot.x())];
        }
    }
    votes.erase(0);
    Q_ASSERT_X(votes.size() <= 1, "computeSpeed", "planted feet disagree on speed");
    return votes.empty() ? 0 : votes.begin()->first;
}

std::array<CatAnimInfo, kAnimCount> buildInfos()
{
    std::array<CatAnimInfo, kAnimCount> infos = {{
        {"idle",      {150, 150, 150, 150, 150, 150},           true},
        {"walk",      {90, 90, 90, 90, 90, 90, 90, 90},         true},
        {"run",       {70, 70, 70, 70, 70, 70},                 true},
        {"sit",       {120, 120, 120, 120},                     false},
        {"sleep",     {300, 300, 300, 300},                     true},
        {"jump_up",   {80, 80, 80},                             false},
        {"fall",      {100, 100},                               true},
        {"land",      {80, 80},                                 false},
        {"paw_swipe", {150, 150, 50, 50, 150},                  false},
        {"climb",     {110, 110, 110, 110, 110, 110, 110, 110}, true},
        {"mantle",    {140, 140, 140},                          false},
        {"roll",      {150, 150, 150, 150},                     true},
        {"sprawl",    {250, 250, 250, 250, 250, 250, 250, 250}, true},
        {"crouch",    {110, 110, 110, 110},                     true},
        {"hang",      {180, 180, 180, 180},                     true},
    }};
    for (int i = 0; i < kAnimCount; ++i) {
        CatAnimInfo &info = infos[i];
        Q_ASSERT(info.frameCount() == kFrames[i]);
        info.speedPxPerFrame = computeSpeed(CatAnim(i), info);
        info.cycleDistancePx = info.speedPxPerFrame * info.frameCount();
    }
    return infos;
}

} // namespace

int CatAnimInfo::totalMs() const
{
    return std::accumulate(frameMs.begin(), frameMs.end(), 0);
}

const CatAnimInfo &animInfo(CatAnim anim)
{
    static const std::array<CatAnimInfo, kAnimCount> infos = buildInfos();
    return infos[std::clamp(int(anim), 0, kAnimCount - 1)];
}

CatPose keyPose(CatAnim anim, int frame)
{
    // animInfo()는 초기화 중 keyPose()를 부르므로 여기서는 kFrames를 쓴다
    const int a = std::clamp(int(anim), 0, kAnimCount - 1);
    return kPoseFns[a](wrap(frame, kFrames[a]));
}

float frameStartT(CatAnim anim, int frame)
{
    const CatAnimInfo &info = animInfo(anim);
    const int n = info.frameCount();
    frame = std::clamp(frame, 0, n);
    const int before = std::accumulate(info.frameMs.begin(), info.frameMs.begin() + frame, 0);
    return float(before) / float(info.totalMs());
}

CatPose poseFor(CatAnim anim, float t)
{
    const CatAnimInfo &info = animInfo(anim);
    const int n = info.frameCount();
    t = info.loop ? t - std::floor(t) : std::clamp(t, 0.0f, 1.0f);

    const float ms = t * float(info.totalMs());
    int i = 0;
    float start = 0;
    while (i < n - 1 && ms >= start + float(info.frameMs[i]) - 1e-3f) {
        start += float(info.frameMs[i]);
        ++i;
    }
    const float u = std::clamp((ms - start) / float(info.frameMs[i]), 0.0f, 1.0f);
    const int next = i + 1 < n ? i + 1 : (info.loop ? 0 : i);
    return CatPose::lerp(keyPose(anim, i), keyPose(anim, next), u);
}

QRect animOpaqueBounds(CatAnim anim, bool intersection)
{
    constexpr int W = CatSprite::Width, H = CatSprite::Width;   // Hang 은 32x32 (나머지는 Height 줄까지만 그려지고 아래는 투명)
    const int n = animInfo(anim).frameCount();

    QRect united;
    bool always[H][W];   // 모든 프레임에서 불투명한 픽셀
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            always[y][x] = true;

    for (int f = 0; f < n; ++f) {
        const QImage img = CatSprite::render(keyPose(anim, f));
        united = united.united(CatSprite::opaqueBounds(img));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                always[y][x] = always[y][x] && y < img.height() && qAlpha(img.pixel(x, y)) > 0;
    }
    if (!intersection)
        return united;

    // 교집합: 모든 프레임에서 빈틈 없이 불투명한 사각형 중 넓이가 가장 큰 것
    // (경계 사각형을 쓰면 귀·꼬리처럼 비어 있는 모서리가 섞여 버튼을 다 덮지 못한다).
    // 넓이가 같으면 폭이 넓은 것, 그래도 같으면 아래쪽 것을 고른다.
    int sum[H + 1][W + 1] = {};   // 불투명하지 않은 픽셀 수의 누적합
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            sum[y + 1][x + 1] = sum[y][x + 1] + sum[y + 1][x] - sum[y][x] + (always[y][x] ? 0 : 1);

    QRect best;
    for (int y0 = 0; y0 < H; ++y0)
        for (int y1 = y0 + 1; y1 <= H; ++y1)
            for (int x0 = 0; x0 < W; ++x0)
                for (int x1 = x0 + 1; x1 <= W; ++x1) {
                    if (sum[y1][x1] - sum[y0][x1] - sum[y1][x0] + sum[y0][x0] != 0)
                        break;   // x1이 커질수록 구멍이 늘기만 한다
                    const QRect r(x0, y0, x1 - x0, y1 - y0);
                    const int area = r.width() * r.height(), bestArea = best.width() * best.height();
                    if (area > bestArea
                        || (area == bestArea && (r.width() > best.width()
                            || (r.width() == best.width() && r.y() > best.y()))))
                        best = r;
                }
    return best;
}
