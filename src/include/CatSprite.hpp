#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>

#include <array>

// 치즈 태비 고양이 픽셀 스프라이트 (cat_modi.png 그림을 정리해 그대로 사용)
// - 기본 방향은 왼쪽, 오른쪽은 캔버스 전체 좌우 반전
// - 얼굴은 항상 3/4 시점, 눈 2개가 모두 보임
// - 파츠(머리/몸통/꼬리/다리 4개)를 겹쳐 그린 뒤 외곽선을 자동으로 두른다

struct CatPalette
{
    // cat_modi.png에서 추출
    QColor outline     {0x31, 0x14, 0x10};
    QColor fur         {0xF9, 0xBD, 0x61};
    QColor shade       {0xE4, 0x91, 0x45};
    QColor cream       {0xFD, 0xF5, 0xE3};
    QColor creamShadow {0xE9, 0xD6, 0xC4};   // 턱 밑 그림자
    QColor innerEar    {0xDC, 0x8A, 0x62};
    QColor nose        {0xE6, 0x8A, 0x74};
    QColor mouth       {0x5A, 0x30, 0x20};
    QColor highlight   {0xFF, 0xFF, 0xFF};   // 뜬 눈 하이라이트
};

enum class CatFacing { Left, Right };

// 화면에 나오는 동물 (팝업 얼굴 버튼으로 고름, QSettings Config::kSettingsSpeciesKey).
// 고양이는 파츠 조합, 강아지는 비트맵 프레임 (DogFrames.inc, py_codes/make_dog_assets.py).
// 움직임(CatPose / CatAnim 키포즈 / 이동 속도)은 모두 같고 그림과 크기만 다르다.
enum class CatSpecies { Cat, WhiteDog, BrownDog };

// 고양이 발이 향하는 방향 = 붙어 있는 면 쪽 (README 3.3)
//   Down : 바닥 (작업표시줄, 창 지붕)         — 변환 없음
//   Left : 창 오른쪽 측면 (벽이 고양이 왼쪽)   — 시계 방향 90°
//   Right: 창 왼쪽 측면 (벽이 고양이 오른쪽)   — 반시계 방향 90°
enum class CatGravity { Down, Left, Right };

// Stretch: 벽타기/넘어가기용 길게 뻗은 몸통, Roll*: 등을 대고 누움(가운데/왼쪽 올림/오른쪽 올림),
// Sprawl: 길게 엎드림, Crouch: 낮춘 사냥 자세(엉덩이를 높이 든 모양), Hang: 앞발로 매달린 사용자 원화 (파츠 아님, 32x32 비트맵 프레임 pose.hangFrame)
enum class CatBody { Stand, Sit, Loaf, LoafInhale,
                     Stretch, Roll, RollLeft, RollRight, Sprawl, Crouch, Hang };
// SprawlA/B/C: 엎드려 바닥에서 살랑,
// RollA/B: 뒹굴 때 흔들, CrouchA/B: 웅크려 실룩일 때 끝만 까딱
enum class CatTail { Up, Sway, Low, Ground,
                     SprawlA, SprawlB, SprawlC, RollA, RollB, CrouchA, CrouchB };


enum CatLegId { NearFront, FarFront, NearBack, FarBack, CatLegCount };

struct CatLeg
{
    // 기본 자세 발끝 위치 기준 오프셋 (스프라이트 px, 왼쪽 보기 좌표, 음수 x = 앞, 음수 y = 위)
    // 발은 지면 좌표라 몸통이 움직여도 따라가지 않는다 (디딤발 고정)
    QPointF foot;
    bool visible = true;
    bool merged = true;    // false면 몸통과의 경계에도 외곽선 (앞발 들기 등)
};

struct CatPose
{
    QPointF body;          // 몸통·꼬리·엉덩이(다리 시작점) 오프셋
    QPointF head;
    CatBody bodyShape = CatBody::Stand;
    CatTail tail = CatTail::Up;
    std::array<CatLeg, CatLegCount> legs{};
    float eyeOpen = 1.0f;  // 0.5 이상 뜬 눈
    bool wideEyes = false; // 눈을 크게 뜸 (3x2 눈, 덮치기 직전). eyeOpen이 뜬 눈일 때만 적용
    int hangFrame = 0;     // bodyShape == Hang 일 때만: 원화에서 만든 대롱대롱 프레임 번호 (나머지 필드는 무시)

    // 연속값은 선형 보간, 형태 선택 같은 이산값은 u >= 0.5에서 b로 전환
    static CatPose lerp(const CatPose &a, const CatPose &b, float u);
};

class CatSprite
{
public:
    // 캔버스: 기본 자세(23x19)를 (OriginX, OriginY)에 두고 사방으로 여유를 둔다.
    // 반전은 캔버스 전체를 뒤집으므로 프레임 사이 위치는 항상 같다.
    static constexpr int Width   = 32;
    static constexpr int Height  = 24;
    static constexpr int OriginX = 4;
    static constexpr int OriginY = 4;

    // 오프셋은 모두 snap()으로 정수 픽셀에 고정한 뒤 그린다 (Pixel 모드)
    static QImage render(const CatPose &pose,
                         CatFacing facing = CatFacing::Left,
                         const CatPalette &palette = CatPalette{});

    // 기본 서 있는 자세
    static QImage render(float eyeOpen,
                         CatFacing facing = CatFacing::Left,
                         const CatPalette &palette = CatPalette{});

    // 붙은 면 방향으로 변환한 스프라이트. facing(좌우 반전)을 먼저, 회전을 나중에 적용한다.
    // 결과 크기: Down → Width×Height, Left/Right → Height×Width
    static QImage renderOriented(const CatPose &pose,
                                 CatGravity gravity,
                                 CatFacing facing = CatFacing::Left,
                                 const CatPalette &palette = CatPalette{});

    // renderOriented() 결과 이미지 안의 앵커 좌표 (스프라이트 px).
    // 앵커 = 기본 프레임의 지면선 중앙 (Width/2, Height-1) 을 같은 변환으로 옮긴 점 (픽셀 경계 좌표)
    //   서 있는 발바닥의 가장 아래 불투명 줄은 Height-2 (22) 줄이고 그 줄의 아래 경계가 Height-1 (23) 이다.
    //   Down (16, 23) / Left (1, 16) / Right (23, 16)
    //   (Left/Right 는 22 줄이 1열/22열로 옮겨지므로 그 열의 바깥 경계. 맨 바깥 열 0/23 은 투명 여백)
    // 벽타기 팔(옆으로 뻗은 다리)은 마지막 줄(Height-1)까지 내려올 수 있어 면 아래로 1 스프라이트 px 파고든다.
    // 강아지: Down (20, 28) / Left (4, 20) / Right (28, 20) — 같은 규칙을 40x32 프레임과 지면선 28 에 적용
    static QPoint anchorIn(CatGravity gravity);

    // Hang 프레임(회전 없는 Width×Width 이미지) 안에서 커서 끝이 놓일 그립 점 (픽셀 경계 좌표):
    // 위로 뻗은 앞발 맨 윗줄(y=0) 가운데 칸의 왼쪽 위 모서리 (16, 0). Right 보기는 좌우 반전 좌표.
    // 모든 Hang 프레임에서 같은 점이다 (README 5.1, 5.13)
    // 강아지: 앞발이 아니라 가슴 가운데 (15, 20) (40x40 프레임, Right 는 (25, 20)). 사용자 지정 (2026-11-22)
    static QPoint hangGripIn(CatFacing facing);

    // ── 동물 종류 ───────────────────────────────────────
    // 한 번에 한 동물만 나오므로 프로세스 전체에 하나. 기본은 Cat 이고, 아래 species 인자 없는 함수들
    // (anchorIn / hangGripIn / frameHeight / canvasSide / stretchLiftSpritePx) 이 현재 동물 기준으로 답한다.
    static void setSpecies(CatSpecies species);
    static CatSpecies species();

    // 강아지 프레임 규격: 40x32 (hang 40x40), 지면선 28 줄 (발 밑 외곽선 27 줄의 아래 경계)
    static constexpr int DogWidth = 40;
    static constexpr int DogHeight = 32;
    static constexpr int DogGroundLine = 28;

    // 현재 동물의 기본 프레임 높이 (고양이 Height, 강아지 DogHeight). 면에 붙었을 때 몸 두께 판정에 쓴다
    static int frameHeight();
    // 오버레이 창 한 변 (스프라이트 px): 회전·매달림 프레임이 모두 들어가는 정사각형 (고양이 Width, 강아지 DogWidth)
    static int canvasSide();
    // Climb / Mantle 프레임의 팔이 지면선 아래로 파고드는 칸 수 (고양이 1, 강아지 0: 강아지 그림은 지면선 위에서 끝남)
    static int stretchLiftSpritePx();

    // 현재 동물로 프레임 하나를 그린다. 고양이는 renderOriented(pose, ...) 와 같고,
    // 강아지는 animIndex(CatAnim 순서)·frame 번호의 비트맵을 facing 반전 → gravity 회전 (hang 은 회전 없음)
    static QImage renderFrame(int animIndex, int frame, const CatPose &pose, CatGravity gravity, CatFacing facing);

    // 동물별 배율 상한: 고양이 Config::kMaxScale, 강아지 Config::kMaxDogScale
    static int maxScale(CatSpecies species);
    // 동물을 from → to 로 바꿀 때의 배율. 강아지 그림이 고양이보다 커서 화면 크기가 비슷하도록 맞춘다.
    //   고양이 → 강아지: 1, 2 → 1 / 3 → 2 / 4 → 3
    //   강아지 → 고양이: 1 → 2 / 2 → 3 / 3 → 4
    //   고양이끼리, 강아지끼리: 그대로. 결과는 항상 to 의 [kMinScale, maxScale] 안
    static int convertScale(int scale, CatSpecies from, CatSpecies to);

    // 동물별 앵커 / 그립 (species 인자 없는 anchorIn / hangGripIn 은 현재 동물)
    static QPoint anchorIn(CatGravity gravity, CatSpecies species);
    static QPoint hangGripIn(CatFacing facing, CatSpecies species);

    // 알파 > 0 픽셀의 경계 사각형 (스프라이트 px). 비어 있으면 null QRect
    static QRect opaqueBounds(const QImage &sprite);

    // 모든 픽셀 스냅에 같은 규칙(0.5는 항상 +쪽)을 써서 프레임마다 반올림이 달라지지 않게 한다
    static int snap(qreal v);
};
