#include "CatSprite.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

// 파츠 맵: 기본 자세 좌표계(23x19, cat_modi.png 고양이 영역)에서 (x0, y0)부터 시작.
//   . 투명   o outline(직접 지정)   f fur   s shade   c cream   b cream 그림자
//   i inner_ear   n nose   d 입
// 바깥 외곽선은 render()가 자동으로 두른다. 맵의 o는 그 외 레퍼런스 고유의 선.
struct PartMap
{
    int x0, y0;
    std::vector<std::string_view> rows;
};

// 머리: 외곽선까지 레퍼런스 그대로 (눈 자리는 바탕색, 눈은 덧그림)
const PartMap kHead = {0, 1, {
    "..o....oo..",
    ".oio..osio.",
    ".oifoofsio.",
    ".ofsssffso.",
    "osfffffffso",
    "osffccffsso",
    "ofcnnccfsso",
    "ofccdccfsso",
    ".ocdcddcbo.",
}};

// 서 있는 몸통 (레퍼런스). 위쪽 b/f 영역은 머리에 가려진 밑그림
const PartMap kBodyStand = {1, 6, {
    "bbbbbbbbff.sssf..",
    "bbbbbbbbffssssfso",
    "bbbbbbbbfffsfsfsf",
    ".bbbbbbbfsfffsfff",
    "..bbbbbssffffffff",
    "...bbbcffffffffff",
    "...ccccffffffffff",
    "...ccccfffffffffs",
    "....fffffsfffsffs",
    "....sssffsofsssff",
}};

// 앉은 자세: 뒷다리는 허벅지 선과 발로 표현
const PartMap kBodySit = {1, 6, {
    "bbbbbbbbf.sss......",
    "bbbbbbbbffsssf.....",
    "bbbbbbbbfffsfsf....",
    ".bbbbbbbfsfffsff...",
    "..bbbbbssfffffff...",
    "...bbbcfffffffffff.",
    "...ccccfffffffofff.",
    "...ccccffffffofffff",
    "....fffffsffoffffff",
    "....sssffsoooffffff",
    "..........ccoffffff",
    "..........ccoffff..",
}};

// 식빵 자세 (잠자기). 앞발 끝만 살짝 보임
const PartMap kBodyLoaf = {4, 9, {
    "........sfsf....",
    "......sfsfsff...",
    "....ffsfsfsfff..",
    ".ffffffffffffff.",
    "cccfffffffffffff",
    "cccfffffffffffff",
    "cccfffffffffffff",
    "fffffffffffffff.",
    "ccoccffffffff...",
}};

// 숨 들이쉼: 등만 1px 올라감
const PartMap kBodyLoafIn = {4, 8, {
    "........sfsf....",
    "......sfsfsff...",
    "....ffsfsfsfff..",
    ".ffffffffffffff.",
    ".ffffffffffffff.",
    "cccfffffffffffff",
    "cccfffffffffffff",
    "cccfffffffffffff",
    "fffffffffffffff.",
    "ccoccffffffff...",
}};

// 세운 꼬리 (레퍼런스)
const PartMap kTailUp = {18, 1, {
    "cc.",
    ".cf",
    ".fs",
    "..f",
    "..f",
    ".f.",
    "ss.",
}};

// 꼬리 끝 1px 흔들림
const PartMap kTailSway = {18, 1, {
    ".cc.",
    "..cf",
    "..fs",
    "..f.",
    "..f.",
    ".f..",
    "ss..",
}};

// 뒤로 뻗은 꼬리 (달리기·점프)
const PartMap kTailLow = {18, 5, {
    "....fcc",
    "..fff..",
    "ssf....",
}};

// 바닥에 내려놓은 꼬리 (앉기·잠자기)
const PartMap kTailGround = {18, 16, {
    ".....c",
    "ffffcc",
}};

// ---- BEGIN 새 파츠 맵 (캔버스 좌표로 그린 뒤 자른 것) ----
// 길게 뻗은 몸통 (벽타기·넘어가기). 서 있는 몸통을 2px 늘이고 위 두 줄을 뺌
const PartMap kBodyStretch = {1, 8, {
    "bbbbbbbbffsfsfsfsfs",
    ".bbbbbbbfsfffsfsffs",
    "..bbbbbssffffffffff",
    "...bbbcffffffsfffff",
    "...ccccffffffffffff",
    "...ccccffffffffffss",
    "....fffffsfffsfffff",
    "....sssffsofsssffss",
}};

// 길게 엎드린 몸통: 앞발 둘을 앞으로 쭉 뻗고, 머리 뒤로 등과 엉덩이 (22줄 o: 바닥 외곽선 끊김 이음)
const PartMap kBodySprawl = {-3, 11, {
    ".....................ssfss.",
    "...................fsfsfffs",
    "..................ffsfffosf",
    ".................ffsfffsofs",
    "..ccssss.........fffsffoffs",
    "ccffsfffffffffffffffsfoffsf",
    "ccffffffsffffffffffsfccofss",
    ".........o.......o.....o...",
}};

// 낮춘 사냥 자세 몸통: 가슴은 낮고 엉덩이가 높이 올라감
const PartMap kBodyCrouch = {10, 8, {
    ".........sssf..",
    ".......ssfsfsf.",
    ".....ssfsfsffsf",
    "...sfsffffsfsff",
    "fsfffffsffosfff",
    "ffsffffffofffff",
    "fffsfffffosffsf",
    "ffffsfffoffsfff",
    "ffsfffsfofffsff",
    "ccfsffccofsffff",
}};

// 등을 대고 누운 몸통 (배 cream이 위)
const PartMap kBodyRoll = {7, 11, {
    "...bbbbbbbbbbb...",
    ".bbcccccccccccbb.",
    "bcccccccccccccccb",
    "ffbcccccccccccbff",
    "fffbbcccccccbbfff",
    "sfffffbbbbbfffffs",
    ".ssfsfffffffsfss.",
}};

// 뒹굴며 왼쪽(머리 쪽)이 올라간 몸통
const PartMap kBodyRollLeft = {7, 10, {
    "...bbbbbb........",
    ".bbbbbbbbbbbbb...",
    "bbbcccccccccccbb.",
    "bcccccccccccccccb",
    "ffbcccccccccccbff",
    "fffbbcccccccbbfff",
    "sfffffbbbbbfffffs",
    ".ssfsfffffffsfss.",
}};

// 뒹굴며 오른쪽(엉덩이 쪽)이 올라간 몸통
const PartMap kBodyRollRight = {7, 10, {
    ".........bbbbb...",
    "...bbbbbbbbbbbbb.",
    ".bbcccccccccccbbb",
    "bcccccccccccccccb",
    "ffbcccccccccccbff",
    "fffbbcccccccbbfff",
    "sfffffbbbbbfffffs",
    ".ssfsfffffffsfss.",
}};

// 엎드려서 바닥에 내려놓은 꼬리
const PartMap kTailSprawlA = {24, 16, {
    "ffc",
    "ffc",
}};

// 꼬리 끝이 살짝 들림
const PartMap kTailSprawlB = {24, 13, {
    "..c",
    ".ff",
    "fff",
    "fff",
    "ff.",
}};

// 꼬리 끝이 크게 들림
const PartMap kTailSprawlC = {24, 9, {
    ".cc",
    ".fc",
    ".ff",
    ".ff",
    ".ff",
    "fff",
    "fff",
    "fff",
    "ff.",
}};

// 뒹굴 때 꼬리: 끝이 살짝 위로
const PartMap kTailRollA = {24, 15, {
    "..c",
    ".ff",
    "ff.",
}};

// 뒹굴 때 꼬리: 바닥에 눕힘
const PartMap kTailRollB = {24, 16, {
    ".fc",
    "ffc",
}};

// 웅크려 실룩일 때 꼬리 (끝이 위)
const PartMap kTailCrouchA = {25, 8, {
    ".c",
    "fc",
    "ff",
    "f.",
}};

// 웅크려 실룩일 때 꼬리 (끝이 옆으로)
const PartMap kTailCrouchB = {25, 10, {
    "f.",
    "ff",
    ".c",
}};

// ---- END 새 파츠 맵 ----

struct Px { int x, y; char key; };

constexpr Px kEyesClosed[] = {
    {1, 6, 'o'}, {2, 5, 'o'}, {3, 6, 'o'},
    {6, 6, 'o'}, {7, 5, 'o'}, {8, 6, 'o'},
};

// 뜬 눈: 2x2, 왼쪽 위 하이라이트 (레퍼런스)
constexpr Px kEyesOpen[] = {
    {2, 5, 'w'}, {3, 5, 'o'}, {2, 6, 'o'}, {3, 6, 'o'},
    {6, 5, 'w'}, {7, 5, 'o'}, {6, 6, 'o'}, {7, 6, 'o'},
};

// 크게 뜬 눈: 2x3, 위 두 줄이 더 길어 동그랗게 보임 (웅크려 덮칠 때)
constexpr Px kEyesWide[] = {
    {2, 4, 'w'}, {3, 4, 'o'}, {2, 5, 'o'}, {3, 5, 'o'}, {2, 6, 'o'}, {3, 6, 'o'},
    {6, 4, 'w'}, {7, 4, 'o'}, {6, 5, 'o'}, {7, 5, 'o'}, {6, 6, 'o'}, {7, 6, 'o'},
};


// 그리는 순서 = z 순서
enum Layer { LFarFront, LFarBack, LTail, LBody, LNearFront, LNearBack, LHead, LayerCount };

struct Canvas
{
    char key[CatSprite::Height][CatSprite::Width];
    signed char owner[CatSprite::Height][CatSprite::Width];

    Canvas()
    {
        for (int y = 0; y < CatSprite::Height; ++y)
            for (int x = 0; x < CatSprite::Width; ++x) {
                key[y][x] = '.';
                owner[y][x] = -1;
            }
    }

    // (x, y)는 기본 자세 좌표
    void put(int x, int y, char k, int layer)
    {
        x += CatSprite::OriginX;
        y += CatSprite::OriginY;
        if (k == '.' || x < 0 || y < 0 || x >= CatSprite::Width || y >= CatSprite::Height)
            return;
        key[y][x] = k;
        owner[y][x] = static_cast<signed char>(layer);
    }

    void putMap(const PartMap &m, int dx, int dy, int layer)
    {
        for (int r = 0; r < int(m.rows.size()); ++r)
            for (int c = 0; c < int(m.rows[r].size()); ++c)
                put(m.x0 + c + dx, m.y0 + r + dy, m.rows[r][c], layer);
    }

    bool filled(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < CatSprite::Width && y < CatSprite::Height
            && key[y][x] != '.' && key[y][x] != 'o';
    }
};

// 2px 두께 다리를 엉덩이에서 발끝까지. 발끝 1줄은 cream (흰 양말)
void drawLeg(Canvas &cv, int hx, int hy, int fx, int fy, char col, int layer)
{
    const int dx = fx - hx, dy = fy - hy;
    if (std::abs(dy) >= std::abs(dx)) {
        const int n = std::abs(dy), sy = dy >= 0 ? 1 : -1;
        for (int i = 0; i <= n; ++i) {
            const int x = hx + (n ? CatSprite::snap(qreal(dx) * i / n) : 0);
            const char k = i == n ? 'c' : col;
            cv.put(x, hy + i * sy, k, layer);
            cv.put(x + 1, hy + i * sy, k, layer);
        }
    } else {   // 앞발을 옆으로 뻗을 때
        const int n = std::abs(dx), sx = dx > 0 ? 1 : -1;
        for (int i = 0; i <= n; ++i) {
            const int y = hy + CatSprite::snap(qreal(dy) * i / n);
            const char k = i == n ? 'c' : col;
            cv.put(hx + i * sx, y, k, layer);
            cv.put(hx + i * sx, y + 1, k, layer);
        }
    }
}

// 몸통 형태마다 다르게 쓰는 값: 몸통 맵, 다리 시작점, 꼬리 위치 보정
struct BodyGeom
{
    const PartMap *map;
    int hipX[CatLegCount];   // NearFront, FarFront, NearBack, FarBack
    int hipY;                // 다리가 시작하는 줄
    int footY;               // foot.y = 0 일 때의 발끝 줄
    bool clampFeet;          // 몸통에 붙은 다리의 발끝을 엉덩이 높이에서 멈춤 (서 있는 계열만)
    int tailDx, tailDy;      // 꼬리 맵 보정 (몸통 offset에 더해짐)
};

const BodyGeom &bodyGeom(CatBody b)
{
    static const BodyGeom stand    = {&kBodyStand,    {8, 5, 16, 13}, 15, 17, true, 0, 0};
    static const BodyGeom sit      = {&kBodySit,      {8, 5, 16, 13}, 15, 17, true, 0, 0};
    static const BodyGeom loaf     = {&kBodyLoaf,     {8, 5, 16, 13}, 15, 17, true, 0, 0};
    static const BodyGeom loafIn   = {&kBodyLoafIn,   {8, 5, 16, 13}, 15, 17, true, 0, 0};
    static const BodyGeom stretch  = {&kBodyStretch,  {8, 5, 18, 15}, 15, 17, true, 2, 2};
    static const BodyGeom roll     = {&kBodyRoll,     {14, 10, 22, 18}, 11, 7, false, 0, 0};
    static const BodyGeom rollL    = {&kBodyRollLeft,  {14, 10, 22, 18}, 11, 7, false, 0, 0};
    static const BodyGeom rollR    = {&kBodyRollRight, {14, 10, 22, 18}, 11, 7, false, 0, 0};
    static const BodyGeom sprawl   = {&kBodySprawl,   {8, 5, 16, 13}, 15, 17, true, 0, 0};
    static const BodyGeom crouch   = {&kBodyCrouch,   {8, 5, 16, 13}, 15, 17, true, 0, 0};
    switch (b) {
    case CatBody::Sit:        return sit;
    case CatBody::Loaf:       return loaf;
    case CatBody::LoafInhale: return loafIn;
    case CatBody::Stretch:    return stretch;
    case CatBody::Roll:       return roll;
    case CatBody::RollLeft:   return rollL;
    case CatBody::RollRight:  return rollR;
    case CatBody::Sprawl:     return sprawl;
    case CatBody::Crouch:     return crouch;
    default:                  return stand;
    }
}

const PartMap &tailMap(CatTail t)
{
    switch (t) {
    case CatTail::Sway:   return kTailSway;
    case CatTail::Low:    return kTailLow;
    case CatTail::Ground: return kTailGround;
    case CatTail::SprawlA: return kTailSprawlA;
    case CatTail::SprawlB: return kTailSprawlB;
    case CatTail::SprawlC: return kTailSprawlC;
    case CatTail::RollA:   return kTailRollA;
    case CatTail::RollB:   return kTailRollB;
    case CatTail::CrouchA: return kTailCrouchA;
    case CatTail::CrouchB: return kTailCrouchB;
    default:              return kTailUp;
    }
}

QRgb colorFor(char key, const CatPalette &p)
{
    switch (key) {
    case 'o': return p.outline.rgb();
    case 'f': return p.fur.rgb();
    case 's': return p.shade.rgb();
    case 'c': return p.cream.rgb();
    case 'i': return p.innerEar.rgb();
    case 'n': return p.nose.rgb();
    case 'w': return p.highlight.rgb();
    case 'b': return p.creamShadow.rgb();
    case 'd': return p.mouth.rgb();
    default:  return qRgba(0, 0, 0, 0);
    }
}

// hang 프레임 비트맵 (사용자 원화, py_codes/make_hang_frames.py 가 생성). 파츠 조합/자동 외곽선 없이 그대로 쓴다
#include "CatHangFrames.inc"

static_assert(sizeof(kHangFrames[0]) / sizeof(kHangFrames[0][0]) == CatSprite::Width,
              "hang frames are Width x Width");

QImage renderHang(int frame, CatFacing facing, const CatPalette &palette)
{
    const char *const *rows = kHangFrames[((frame % kHangFrameCount) + kHangFrameCount) % kHangFrameCount];
    QImage img(CatSprite::Width, CatSprite::Width, QImage::Format_ARGB32);
    for (int y = 0; y < CatSprite::Width; ++y)
        for (int x = 0; x < CatSprite::Width; ++x)
            img.setPixel(x, y, colorFor(rows[y][x], palette));
    if (facing == CatFacing::Right)
        img = img.flipped(Qt::Horizontal);
    return img;
}

} // namespace

int CatSprite::snap(qreal v)
{
    return int(std::floor(v + 0.5));
}

CatPose CatPose::lerp(const CatPose &a, const CatPose &b, float u)
{
    const auto mix = [u](const QPointF &p, const QPointF &q) { return p + (q - p) * u; };
    const bool second = u >= 0.5f;

    CatPose r = second ? b : a;
    r.body = mix(a.body, b.body);
    r.head = mix(a.head, b.head);
    r.eyeOpen = a.eyeOpen + (b.eyeOpen - a.eyeOpen) * u;
    for (int i = 0; i < CatLegCount; ++i)
        r.legs[i].foot = mix(a.legs[i].foot, b.legs[i].foot);
    return r;
}

QImage CatSprite::render(const CatPose &pose, CatFacing facing, const CatPalette &palette)
{
    if (pose.bodyShape == CatBody::Hang)   // 매달림은 파츠 조합이 아니라 32x32 원화 프레임 (README 5.1)
        return renderHang(pose.hangFrame, facing, palette);

    const int bx = snap(pose.body.x()), by = snap(pose.body.y());
    const int hx = snap(pose.head.x()), hy = snap(pose.head.y());

    Canvas cv;

    const BodyGeom &geom = bodyGeom(pose.bodyShape);

    const auto leg = [&](CatLegId id, int layer) {
        const CatLeg &l = pose.legs[id];
        if (!l.visible)
            return;
        const char col = (id == FarFront || id == FarBack) ? 's' : 'f';
        const int hipY = geom.hipY + by;
        int footY = geom.footY + snap(l.foot.y());
        if (l.merged && geom.clampFeet)     // 다리가 짧아 발을 엉덩이보다 높이 들면 뒤집히므로
            footY = std::max(footY, hipY);  // 몸통에 붙은 다리는 발끝을 엉덩이 높이에서 멈춤
        drawLeg(cv, geom.hipX[id] + bx, hipY, geom.hipX[id] + snap(l.foot.x()), footY, col, layer);
    };

    leg(FarFront, LFarFront);
    leg(FarBack, LFarBack);
    cv.putMap(tailMap(pose.tail), bx + geom.tailDx, by + geom.tailDy, LTail);
    cv.putMap(*geom.map, bx, by, LBody);
    leg(NearFront, LNearFront);
    leg(NearBack, LNearBack);
    cv.putMap(kHead, hx, hy, LHead);

    // 외곽선 없이 이어 붙는 파츠 쌍 (몸통-꼬리, 몸통-다리)
    bool merged[LayerCount][LayerCount] = {};
    const auto merge = [&](int a, int b) { merged[a][b] = merged[b][a] = true; };
    merge(LBody, LTail);
    merge(LBody, LHead);    // 레퍼런스는 턱 밑에 선이 없다 (목 옆 선은 머리 맵에 직접 있음)
    if (pose.legs[FarFront].merged)  merge(LBody, LFarFront);
    if (pose.legs[FarBack].merged)   merge(LBody, LFarBack);
    if (pose.legs[NearFront].merged) merge(LBody, LNearFront);
    if (pose.legs[NearBack].merged)  merge(LBody, LNearBack);

    // 외곽선: 바깥은 빈 칸에, 파츠 사이 경계는 뒤쪽 파츠 쪽에 1px.
    // 상하좌우 4방향만 보므로 대각선 모서리는 자연스럽게 깎인다.
    char out[Height][Width];
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            const char k = cv.key[y][x];
            out[y][x] = k;
            const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
            if (k == '.') {
                for (const auto &n : nb)
                    if (cv.filled(n[0], n[1])) { out[y][x] = 'o'; break; }
            } else if (k != 'o') {
                const int a = cv.owner[y][x];
                for (const auto &n : nb) {
                    if (!cv.filled(n[0], n[1]))
                        continue;
                    const int b = cv.owner[n[1]][n[0]];
                    if (b > a && !merged[a][b]) { out[y][x] = 'o'; break; }
                }
            }
        }

    // 눈은 외곽선 계산 뒤에 덧그림
    const auto drawEye = [&](const Px &p) {
        const int x = p.x + hx + OriginX, y = p.y + hy + OriginY;
        if (x >= 0 && y >= 0 && x < Width && y < Height)
            out[y][x] = p.key;
    };
    if (pose.eyeOpen >= 0.5f) {
        if (pose.wideEyes) {
            for (const Px &p : kEyesWide)
                drawEye(p);
        } else {
            for (const Px &p : kEyesOpen)
                drawEye(p);
        }
    } else {
        for (const Px &p : kEyesClosed)
            drawEye(p);
    }

    QImage img(Width, Height, QImage::Format_ARGB32);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            img.setPixel(x, y, colorFor(out[y][x], palette));

    if (facing == CatFacing::Right)
        img = img.flipped(Qt::Horizontal);
    return img;
}

QImage CatSprite::render(float eyeOpen, CatFacing facing, const CatPalette &palette)
{
    CatPose pose;
    pose.eyeOpen = eyeOpen;
    return render(pose, facing, palette);
}

QImage CatSprite::renderOriented(const CatPose &pose, CatGravity gravity, CatFacing facing,
                                 const CatPalette &palette)
{
    const QImage base = render(pose, facing, palette);   // 좌우 반전은 render()에서 먼저 적용됨
    // Hang 은 항상 Down 으로만 쓰는 32x32 프레임이라 회전하지 않는다 (Height×Width 로 돌릴 수 없음)
    if (gravity == CatGravity::Down || pose.bodyShape == CatBody::Hang)
        return base;

    // 90° 회전은 픽셀을 그대로 옮기므로 무손실. (x, y) → 시계 방향 (Height-1-y, x), 반시계 방향 (y, Width-1-x)
    QImage out(Height, Width, QImage::Format_ARGB32);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            const QRgb px = base.pixel(x, y);
            if (gravity == CatGravity::Left)
                out.setPixel(Height - 1 - y, x, px);
            else
                out.setPixel(y, Width - 1 - x, px);
        }
    return out;
}

QPoint CatSprite::anchorIn(CatGravity gravity)
{
    // 서 있는 발바닥의 가장 아래 불투명 줄은 Height-2 (= 22) 줄이고, 그 줄의 아래 경계가 지면선(y = Height-1 = 23)이다.
    // 기본 프레임의 앵커 (Width/2, Height-1) 은 픽셀 경계 좌표이므로 연속 좌표 회전 공식을 그대로 쓴다.
    //   시계 방향:   (x, y) → (Height - y, x)   → (1, Width/2)       (22 줄이 1열로 가므로 그 열의 바깥 경계)
    //   반시계 방향: (x, y) → (y, Width - x)    → (Height-1, Width/2) (22 줄이 22열로 가므로 그 열의 바깥 경계)
    constexpr int kGroundLine = Height - 1;
    switch (gravity) {
    case CatGravity::Left:  return QPoint(Height - kGroundLine, Width / 2);
    case CatGravity::Right: return QPoint(kGroundLine, Width / 2);
    default:                return QPoint(Width / 2, kGroundLine);
    }
}

QPoint CatSprite::hangGripIn(CatFacing facing)
{
    // 위로 뻗은 앞발 끝 맨 윗줄(y=0, 3칸 x=15..17)의 가운데 칸 x=16 의 왼쪽 위 모서리 (픽셀 경계 좌표).
    // anchorIn 이 픽셀 경계 좌표(Width/2, Height-1)를 쓰는 것과 같은 규칙이라 커서 끝이 앞발 맨 윗줄 윗변에 닿는다.
    // 정수 좌표라 가운데 칸의 중앙(16.5)에서 반 칸 왼쪽이며, Right 는 캔버스 좌우 반전 좌표 (Width - 16 = 16, 가운데 칸의 오른쪽 변)
    constexpr int kGripX = 16, kGripY = 0;
    return QPoint(facing == CatFacing::Right ? Width - kGripX : kGripX, kGripY);
}

QRect CatSprite::opaqueBounds(const QImage &sprite)
{
    int x0 = sprite.width(), y0 = sprite.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < sprite.height(); ++y)
        for (int x = 0; x < sprite.width(); ++x)
            if (qAlpha(sprite.pixel(x, y)) > 0) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    if (x1 < 0)
        return QRect();
    return QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}
