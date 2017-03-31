#include "stdafx.h"
#include "rf.h"
#include "rfproto.h"
#include "spectate_mode.h"
#include "BuildConfig.h"
#include "utils.h"
#include "scoreboard.h"

#include "hooks/HookCall.h"

using namespace rf;

#if SPECTATE_MODE_ENABLE

static CPlayer *g_SpectateModeTarget;
static PlayerCamera *g_OldTargetCamera = NULL;
static bool g_SpectateModeEnabled = false;
static int g_LargeFont = -1, g_MediumFont = -1, g_SmallFont = -1;

auto HandleCtrlInGame_Hook = makeFunHook(HandleCtrlInGame);
auto RenderReticle_Hook = makeFunHook(RenderReticle);
auto PlayerCreateEntity_Hook = makeFunHook(PlayerCreateEntity);

static void SetCameraTarget(CPlayer *pPlayer)
{
    // Based on function SetCamera1View
    if (!*g_ppLocalPlayer || !(*g_ppLocalPlayer)->pCamera || !pPlayer)
        return;

    PlayerCamera *pCamera = (*g_ppLocalPlayer)->pCamera;
    pCamera->Type = PlayerCamera::CAM_1ST_PERSON;
    pCamera->pPlayer = pPlayer;

    g_OldTargetCamera = pPlayer->pCamera;
    pPlayer->pCamera = pCamera; // fix crash 0040D744

    EntityObj *pEntity = HandleToEntity(pPlayer->hEntity);
    if (pEntity)
    {
        EntityObj *pCamEntity = pCamera->pCameraEntity;
        pCamEntity->Head.hParent = pPlayer->hEntity;
        pCamEntity->Head.vPos = pEntity->vWeaponPos;
        pCamEntity->Head.matRot = pEntity->Head.matRot;
        pCamEntity->matWeaponRot = pEntity->matWeaponRot;
        pCamEntity->Head.field_0 = pEntity->Head.field_0;
        pCamEntity->Head.field_4 = pEntity->Head.vPos;
    }
}

void SpectateModeSetTargetPlayer(CPlayer *pPlayer)
{
    if (!pPlayer)
        pPlayer = *g_ppLocalPlayer;

    if (!*g_ppLocalPlayer || !(*g_ppLocalPlayer)->pCamera || !g_SpectateModeTarget || g_SpectateModeTarget == pPlayer)
        return;

    if (*g_pGameOptions & RF_GO_FORCE_RESPAWN)
    {
        CString strMessage, strPrefix;
        CString_Init(&strMessage, "You cannot use Spectate Mode because Force Respawn option is enabled on this server!");
        CString_InitEmpty(&strPrefix);
        ChatPrint(strMessage, 4, strPrefix);
        return;
    }

    // fix old target
    if (g_SpectateModeTarget && g_SpectateModeTarget != *g_ppLocalPlayer)
    {
        g_SpectateModeTarget->pCamera = g_OldTargetCamera;
        g_OldTargetCamera = NULL;

#if SPECTATE_MODE_SHOW_WEAPON
        g_SpectateModeTarget->FireFlags &= ~(1 << 4);
        EntityObj *pEntity = HandleToEntity(g_SpectateModeTarget->hEntity);
        if (pEntity)
            pEntity->pLocalPlayer = NULL;
#endif // SPECTATE_MODE_SHOW_WEAPON
    }

    g_SpectateModeEnabled = (pPlayer != *g_ppLocalPlayer);
    g_SpectateModeTarget = pPlayer;

    KillLocalPlayer();
    SetCameraTarget(pPlayer);

#if SPECTATE_MODE_SHOW_WEAPON
    pPlayer->FireFlags |= 1 << 4;
    EntityObj *pEntity = HandleToEntity(pPlayer->hEntity);
    if (pEntity)
    {
        // make sure weapon mesh is loaded now
        PlayerFpgunSetupMesh(pPlayer, pEntity->WeaponSel.WeaponClsId);
        TRACE("pWeaponMesh %p", pPlayer->pWeaponMesh);

        // Hide target player from camera
        pEntity->pLocalPlayer = pPlayer;
    }
#endif // SPECTATE_MODE_SHOW_WEAPON
}

static void SpectateNextPlayer(bool bDir, bool bTryAlivePlayersFirst = false)
{
    CPlayer *pNewTarget;
    if (g_SpectateModeEnabled)
        pNewTarget = g_SpectateModeTarget;
    else
        pNewTarget = *g_ppLocalPlayer;
    while (true)
    {
        pNewTarget = bDir ? pNewTarget->pNext : pNewTarget->pPrev;
        if (!pNewTarget || pNewTarget == g_SpectateModeTarget)
            break; // nothing found
        if (bTryAlivePlayersFirst && IsPlayerEntityInvalid(pNewTarget))
            continue;
        if (pNewTarget != *g_ppLocalPlayer)
        {
            SpectateModeSetTargetPlayer(pNewTarget);
            return;
        }
    }

    if (bTryAlivePlayersFirst)
        SpectateNextPlayer(bDir, false);
}

static void HandleCtrlInGameHook(CPlayer *pPlayer, EGameCtrl KeyId, char WasPressed)
{
    if (g_SpectateModeEnabled)
    {
        if (KeyId == GC_PRIMARY_ATTACK || KeyId == GC_SLIDE_RIGHT)
        {
            if (WasPressed)
                SpectateNextPlayer(true);
            return; // dont allow spawn
        }
        else if (KeyId == GC_SECONDARY_ATTACK || KeyId == GC_SLIDE_LEFT)
        {
            if (WasPressed)
                SpectateNextPlayer(false);
            return;
        }
        else if (KeyId == GC_JUMP)
        {
            if (WasPressed)
                SpectateModeSetTargetPlayer(nullptr);
            return;
        }
    }
    else if (!g_SpectateModeEnabled)
    {
        if (KeyId == GC_JUMP && WasPressed && IsPlayerEntityInvalid(*g_ppLocalPlayer))
        {
            SpectateModeSetTargetPlayer(*g_ppLocalPlayer);
            SpectateNextPlayer(true, true);
            return;
        }
    }
    
    HandleCtrlInGame_Hook.callTrampoline(pPlayer, KeyId, WasPressed);
}

static bool IsPlayerEntityInvalidHook(CPlayer *pPlayer)
{
    if (g_SpectateModeEnabled)
        return false;
    else
        return IsPlayerEntityInvalid(pPlayer);
}

static bool IsPlayerDyingHook(CPlayer *pPlayer)
{
    if (g_SpectateModeEnabled)
        return false;
    else
        return IsPlayerDying(pPlayer);
}

void SpectateModeOnDestroyPlayer(CPlayer *pPlayer)
{
    if (g_SpectateModeTarget == pPlayer)
        SpectateNextPlayer(true);
    if (g_SpectateModeTarget == pPlayer)
        SpectateModeSetTargetPlayer(nullptr);
}

static void RenderReticle_New(CPlayer *pPlayer)
{
    if (GetCurrentMenuId() == MENU_MP_LIMBO)
        return;
    if (g_SpectateModeEnabled)
        RenderReticle_Hook.callTrampoline(g_SpectateModeTarget);
    else
        RenderReticle_Hook.callTrampoline(pPlayer);
}


EntityObj *PlayerCreateEntity_New(CPlayer *pPlayer, int ClassId, const CVector3 *pPos, const CMatrix3 *pRotMatrix, int MpCharacter)
{
    // hide target player from camera after respawn
    EntityObj *pEntity = PlayerCreateEntity_Hook.callTrampoline(pPlayer, ClassId, pPos, pRotMatrix, MpCharacter);
    if (pEntity && pPlayer == g_SpectateModeTarget)
        pEntity->pLocalPlayer = pPlayer;
    return pEntity;
}
#if SPECTATE_MODE_SHOW_WEAPON

static void PlayerFpgunRender_New(CPlayer *pPlayer)
{
    if (g_SpectateModeEnabled)
    {
        EntityObj *pEntity = HandleToEntity(g_SpectateModeTarget->hEntity);

        // HACKFIX: RF uses function PlayerSetRemoteChargeVisible for local player only
        g_SpectateModeTarget->RemoteChargeVisible = (pEntity && pEntity->WeaponSel.WeaponClsId == *g_pRemoteChargeClsId);

        PlayerFpgunUpdateMesh(g_SpectateModeTarget);
        PlayerFpgunRender(g_SpectateModeTarget);
    }
    else
        PlayerFpgunRender(pPlayer);
}

#endif // SPECTATE_MODE_SHOW_WEAPON

void SpectateModeInit()
{
    static HookCall<IsPlayerEntityInvalid_Type> IsPlayerEntityInvalid_RedBars_Hookable(0x00432A52, IsPlayerEntityInvalid);
    IsPlayerEntityInvalid_RedBars_Hookable.Hook(IsPlayerEntityInvalidHook);

    static HookCall<IsPlayerDying_Type> IsPlayerDying_RedBars_Hookable(0x00432A5F, IsPlayerDying);
    IsPlayerDying_RedBars_Hookable.Hook(IsPlayerDyingHook);

    static HookCall<IsPlayerEntityInvalid_Type> IsPlayerEntityInvalid_Scoreboard_Hookable(0x00437BEE, IsPlayerEntityInvalid);
    IsPlayerEntityInvalid_Scoreboard_Hookable.Hook(IsPlayerEntityInvalidHook);

    static HookCall<IsPlayerDying_Type> IsPlayerDying_Scoreboard_Hookable(0x00437C01, IsPlayerDying);
    IsPlayerDying_Scoreboard_Hookable.Hook(IsPlayerDyingHook);

    static HookCall<IsPlayerEntityInvalid_Type> IsPlayerEntityInvalid_Scoreboard_Hookable2(0x00437C25, IsPlayerEntityInvalid);
    IsPlayerEntityInvalid_Scoreboard_Hookable2.Hook(IsPlayerEntityInvalidHook);

    static HookCall<IsPlayerDying_Type> IsPlayerDying_Scoreboard_Hookable2(0x00437C36, IsPlayerDying);
    IsPlayerDying_Scoreboard_Hookable2.Hook(IsPlayerDyingHook);
    
    HandleCtrlInGame_Hook.hook(HandleCtrlInGameHook);
    RenderReticle_Hook.hook(RenderReticle_New);
    PlayerCreateEntity_Hook.hook(PlayerCreateEntity_New);

    // Note: HUD rendering doesn't make sense because life and armor isn't synced

#if SPECTATE_MODE_SHOW_WEAPON
    WriteMemInt32(0x0043285D + 1, (uintptr_t)PlayerFpgunRender_New - (0x0043285D + 0x5));
    WriteMemUInt8(0x004AB1B8, ASM_NOP, 6); // PlayerFpgunRenderInternal
    WriteMemUInt8(0x004AA23E, ASM_NOP, 6); // PlayerFpgunSetupMesh
    WriteMemUInt8(0x004AE0DF, ASM_NOP, 2); // PlayerFpgunLoadMesh

    WriteMemUInt8(0x004AA3B1, ASM_NOP, 6); // sub_4AA3A0
    WriteMemUInt8(0x004A952C, ASM_SHORT_JMP_REL); // sub_4A9520
    WriteMemUInt8(0x004AA56D, ASM_NOP, 6); // sub_4AA560
    WriteMemUInt8(0x004AE384, ASM_NOP, 6); // PlayerFpgunPrepareWeapon
    WriteMemUInt8(0x004AA6E7, ASM_NOP, 6); // PlayerFpgunUpdateMesh

    WriteMemPtr(0x0048857E + 2, &g_SpectateModeTarget); // RenderObjects
    WriteMemPtr(0x00488598 + 1, &g_SpectateModeTarget); // RenderObjects

#endif // SPECTATE_MODE_SHOW_WEAPON
    
}

void SpectateModeAfterFullGameInit()
{
    g_SpectateModeTarget = *g_ppLocalPlayer;
}

void SpectateModeDrawUI()
{
    if (!g_SpectateModeEnabled)
    {
        if (IsPlayerEntityInvalid(*g_ppLocalPlayer))
        {
            GrSetColor(0xFF, 0xFF, 0xFF, 0xFF);
            GrDrawAlignedText(GR_ALIGN_LEFT, 20, 200, "Press JUMP key to enter Spectate Mode", -1, *g_pGrTextMaterial);
        }
        return;
    }
    
    if (g_LargeFont == -1)
        g_LargeFont = GrLoadFont("rfpc-large.vf", -1);
    if (g_MediumFont == -1)
        g_MediumFont = GrLoadFont("rfpc-medium.vf", -1);
    if (g_SmallFont == -1)
        g_SmallFont = GrLoadFont("rfpc-small.vf", -1);

    const unsigned cx = 500, cy = 50;
    unsigned cxScr = GrGetMaxWidth(), cySrc = GrGetMaxHeight();
    unsigned x = (cxScr - cx) / 2;
    unsigned y = cySrc - 100;
    unsigned cyFont = GrGetFontHeight(-1);

    GrSetColor(0, 0, 0, 0x80);
    GrDrawAlignedText(GR_ALIGN_CENTER, cxScr / 2 + 2, 150 + 2, "SPECTATE MODE", g_LargeFont, *g_pGrTextMaterial);
    GrSetColor(0xFF, 0xFF, 0xFF, 0xFF);
    GrDrawAlignedText(GR_ALIGN_CENTER, cxScr / 2, 150, "SPECTATE MODE", g_LargeFont, *g_pGrTextMaterial);

    GrSetColor(0xFF, 0xFF, 0xFF, 0xFF);
    GrDrawAlignedText(GR_ALIGN_LEFT, 20, 200, "Press JUMP key to exit Spectate Mode", g_MediumFont, *g_pGrTextMaterial);
    GrDrawAlignedText(GR_ALIGN_LEFT, 20, 215, "Press PRIMARY ATTACK key to switch to the next player", g_MediumFont, *g_pGrTextMaterial);
    GrDrawAlignedText(GR_ALIGN_LEFT, 20, 230, "Press SECONDARY ATTACK key to switch to the previous player", g_MediumFont, *g_pGrTextMaterial);

    GrSetColor(0, 0, 0x00, 0x60);
    GrDrawRect(x, y, cx, cy, *g_pGrRectMaterial);

    char szBuf[256];
    GrSetColor(0xFF, 0xFF, 0, 0x80);
    snprintf(szBuf, sizeof(szBuf), "Spectating: %s", CString_CStr(&g_SpectateModeTarget->strName));
    GrDrawAlignedText(GR_ALIGN_CENTER, x + cx / 2, y + cy / 2 - cyFont / 2 - 5, szBuf, g_LargeFont, *g_pGrTextMaterial);

    EntityObj *pEntity = HandleToEntity(g_SpectateModeTarget->hEntity);
    if (!pEntity)
    {
        GrSetColor(0xC0, 0, 0, 0xC0);
        GrDrawAlignedText(GR_ALIGN_CENTER, cxScr / 2, cySrc / 2, "DEAD", g_LargeFont, *g_pGrTextMaterial);
    }
}

bool SpectateModeIsActive()
{
    return g_SpectateModeEnabled;
}

#endif // SPECTATE_MODE_ENABLE
