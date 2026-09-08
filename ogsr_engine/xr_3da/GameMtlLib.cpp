#include "stdafx.h"

#include "GameMtlLib.h"

namespace xxh
{
#include <xxhash.h>
}

CGameMtlLibrary GMLib;

void SGameMtl::Load(IReader& fs)
{
    XR_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_MAIN) > 0);
    ID = fs.r_u32();
    fs.r_stringZ(m_Name);

    if (fs.find_chunk(GAMEMTL_CHUNK_DESC) > 0)
        fs.r_stringZ(m_Desc);

    XR_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FLAGS) > 0);
    Flags.assign(fs.r_u32());

    XR_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_PHYSICS) > 0);
    fPHFriction = fs.r_float();
    fPHDamping = fs.r_float();
    fPHSpring = fs.r_float();
    fPHBounceStartVelocity = fs.r_float();
    fPHBouncing = fs.r_float();

    XR_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FACTORS) > 0);
    fShootFactor = fs.r_float();
    fBounceDamageFactor = fs.r_float();
    fVisTransparencyFactor = fs.r_float();
    fSndOcclusionFactor = fs.r_float();

    if (fs.find_chunk(GAMEMTL_CHUNK_FACTORS_MP) > 0)
        fShootFactorMP = fs.r_float();
    else
        fShootFactorMP = fShootFactor;

    if (fs.find_chunk(GAMEMTL_CHUNK_FLOTATION) > 0)
        fFlotationFactor = fs.r_float();

    if (fs.find_chunk(GAMEMTL_CHUNK_INJURIOUS) > 0)
        fInjuriousSpeed = fs.r_float();

    if (fs.find_chunk(GAMEMTL_CHUNK_DENSITY) > 0)
        fDensityFactor = fs.r_float();
}

CGameMtlLibrary::CGameMtlLibrary()
{
    material_index = 0;
    material_pair_index = 0;
}

void CGameMtlLibrary::Load()
{
    string_path name;
    if (!FS.exist(name, _game_data_, GAMEMTL_FILENAME))
    {
        Msg("! Can't find game material file: [{}]", name);
        return;
    }

    XR_ASSERT(material_pairs.size() == 0, "", material_pairs);
    XR_ASSERT(materials.size() == 0, "", materials);

    IReader* F = FS.r_open(name);
    XR_ASSERT(F->find_chunk(GAMEMTLS_CHUNK_VERSION) > 0);

    if (F->r_u16() != GAMEMTL_CURRENT_VERSION)
    {
        XR_LOG_ERROR("Invalid version. Library can't load");

        FS.r_close(F);
        return;
    }

    XR_ASSERT(F->find_chunk(GAMEMTLS_CHUNK_AUTOINC) > 0);
    material_index = F->r_u32();
    material_pair_index = F->r_u32();

    materials.reserve(material_index);
    material_pairs.reserve(material_pair_index);

    IReader* OBJ = F->open_chunk(GAMEMTLS_CHUNK_MTLS);
    if (OBJ)
    {
        u32 count;
        for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count, O))
        {
            SGameMtl* M = materials.emplace_back(xr_new<SGameMtl>());
            M->Load(*O);
        }
        OBJ->close();
    }

    OBJ = F->open_chunk(GAMEMTLS_CHUNK_MTLS_PAIR);
    if (OBJ)
    {
        u32 count;
        for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count, O))
        {
            SGameMtlPair* M = material_pairs.emplace_back(xr_new<SGameMtlPair>(this));
            M->Load(*O);
        }
        OBJ->close();
    }

    const u32 mtlCount = materials.size();
    material_pairs_rt.resize(mtlCount * mtlCount, nullptr);

    for (const auto& mtlPair : material_pairs)
    {
        const u16 idx_1 = GetMaterialIdx(mtlPair->mtl0);
        const u16 idx_2 = GetMaterialIdx(mtlPair->mtl1);

        if (idx_1 >= mtlCount || idx_2 >= mtlCount)
        {
            Msg("~ Wrong material pars: mtl0=[{}] mtl1=[{}]", mtlPair->mtl0, mtlPair->mtl1);
            continue;
        }

        const int idx0 = idx_1 * mtlCount + idx_2;
        const int idx1 = idx_2 * mtlCount + idx_1;
        material_pairs_rt[idx0] = mtlPair;
        material_pairs_rt[idx1] = mtlPair;
    }

    FS.r_close(F);
}

u64 CGameMtlLibrary::get_hash()
{
    xxh::XXH64_hash_t xxh = std::numeric_limits<xxh::XXH64_hash_t>::max();
    string_path name;

    if (!FS.exist(name, _game_data_, GAMEMTL_FILENAME))
        return xxh;

    IReader* r = FS.r_open(name);
    xxh = xxh::XXH3_64bits(r->pointer(), r->length());
    FS.r_close(r);

    return xxh;
}

#ifdef DEBUG
const char* SGameMtlPair::dbg_Name() const
{
    static string256 nm;
    const SGameMtl* M0 = GMLib.GetMaterialByID(GetMtl0());
    const SGameMtl* M1 = GMLib.GetMaterialByID(GetMtl1());
    xr_sprintf(nm, sizeof(nm), "Pair: %s - %s", *M0->m_Name, *M1->m_Name);
    return nm;
}
#endif
