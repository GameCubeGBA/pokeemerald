#ifndef GUARD_INTRO_CREDITS_GRAPHICS_H
#define GUARD_INTRO_CREDITS_GRAPHICS_H

// States for gIntroCredits_MovingSceneryState
enum {
    INTROCRED_SCENERY_NORMAL,
    INTROCRED_SCENERY_DESTROY,
    INTROCRED_SCENERY_FROZEN,
};

// Scenes for the Credits sequence
enum {
    SCENE_OCEAN_MORNING,
    SCENE_OCEAN_SUNSET,
    SCENE_FOREST_RIVAL_ARRIVE,
    SCENE_FOREST_CATCH_RIVAL,
    SCENE_CITY_NIGHT,
};

extern u16 gIntroCredits_MovingSceneryVBase;
extern s16 gIntroCredits_MovingSceneryVOffset;
extern s16 gIntroCredits_MovingSceneryState;

extern const struct SpriteSheet gSpriteSheet_IntroBrendan[];
extern const struct SpriteSheet gSpriteSheet_IntroMay[];
extern const struct SpriteSheet gSpriteSheet_IntroBicycle[];
extern const struct SpriteSheet gSpriteSheet_IntroFlygon[];
extern const struct SpritePalette gSpritePalettes_IntroPlayerFlygon[];
extern const struct SpriteSheet gSpriteSheet_CreditsBrendan[];
extern const struct SpriteSheet gSpriteSheet_CreditsMay[];
extern const struct SpriteSheet gSpriteSheet_CreditsBicycle[];
extern const struct SpriteSheet gSpriteSheet_CreditsRivalBrendan[];
extern const struct SpriteSheet gSpriteSheet_CreditsRivalMay[];
extern const struct SpritePalette gSpritePalettes_Credits[];

void LoadIntroPart2Graphics(void);
void SetIntroPart2BgCnt(u8 scenery);
void LoadCreditsSceneGraphics(u8);
void SetCreditsSceneBgCnt(u8);
u8 CreateBicycleBgAnimationTask(u8 mode, u16 bg1Speed, u16 bg2Speed, u16 bg3Speed);
void CycleSceneryPalette(u8);
u8 CreateIntroBrendanSprite(s16 x, s16 y);
u8 CreateIntroMaySprite(s16 x, s16 y);
u8 CreateIntroFlygonSprite(s16 x, s16 y);

#endif // GUARD_INTRO_CREDITS_GRAPHICS_H
