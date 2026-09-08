////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_storage_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator storage manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "alife_storage_manager.h"

#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_group_registry.h"
#include "alife_registry_container.h"
#include "xrServer.h"
#include "Level.h"
#include "../xr_3da/x_ray.h"
#include "saved_game_wrapper.h"
#include "string_table.h"
#include "../xr_3da/IGame_Persistent.h"
#include "script_engine.h"

namespace zstd
{
#include <zstd.h>
}

using namespace ALife;

void CALifeStorageManager::save(LPCSTR save_name, bool update_name)
{
    strcpy_s(g_last_saved_game, save_name);
    string_path save;
    strcpy_s(save, m_save_name);

    if (save_name)
    {
        strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
    }
    else if (xr_strlen(m_save_name) == 0)
    {
        XR_LOG_ERROR("No file name specified!");
        return;
    }

    u32 source_count;
    u32 dest_count;
    void* dest_data;
    {
        CMemoryWriter stream;
        stream.reserve(4 * 1024 * 1024);
        header().save(stream);
        time_manager().save(stream);
        spawns().save(stream);
        objects().save(stream);
        registry().save(stream);

        if (pSettings->line_exist("engine_callbacks", "after_objs_save"))
        {
            const char* callback = pSettings->r_string("engine_callbacks", "after_objs_save");

            if (sol::function lua_function; ai().script_engine().function(callback, lua_function))
            {
                stream.open_chunk(CALLBACK_CHUNK_DATA);
                lua_function((IWriter*)&stream);
                stream.close_chunk();
            }
        }

        source_count = stream.tell();
        void* source_data = stream.pointer();
        dest_count = zstd::ZSTD_compressBound(source_count);
        dest_data = xr_malloc(dest_count);

        dest_count = zstd::ZSTD_compress(dest_data, dest_count, source_data, source_count, 1);
        R_ASSERT(!zstd::ZSTD_isError(dest_count));
    }

    string_path temp;
    std::ignore = FS.update_path(temp, "$game_saves$", m_save_name);
    IWriter* writer = FS.w_open(temp);
    writer->w_u32(u32(-1));
    writer->w_u32(ALIFE_VERSION);

    writer->w_u32(source_count);
    writer->w(dest_data, dest_count);
    xr_free(dest_data);
    FS.w_close(writer);

#ifdef DEBUG
    Msg("* Game {} is successfully saved to file '{}' ({} bytes compressed to {})", m_save_name, temp, source_count, dest_count + 4);
#else // DEBUG
    Msg("* Game {} is successfully saved to file '{}'", m_save_name, temp);
#endif // DEBUG

    if (!update_name)
        strcpy_s(m_save_name, save);
}

void CALifeStorageManager::load(void* buffer, const u32& buffer_size, LPCSTR file_name)
{
    IReader source(buffer, buffer_size);
    header().load(source);
    time_manager().load(source);
    spawns().load(source, file_name);

    graph().on_load();
    objects().load(source);

    VERIFY(can_register_objects());
    can_register_objects(false);
    CALifeObjectRegistry::OBJECT_REGISTRY::iterator B = objects().objects().begin();
    CALifeObjectRegistry::OBJECT_REGISTRY::iterator E = objects().objects().end();
    CALifeObjectRegistry::OBJECT_REGISTRY::iterator I;
    for (I = B; I != E; ++I)
    {
        ALife::_OBJECT_ID id = (*I).second->ID;
        (*I).second->ID = server().PerformIDgen(id);
        VERIFY(id == (*I).second->ID);
        register_object((*I).second, false);
    }

    registry().load(source);

    if (pSettings->line_exist("engine_callbacks", "after_objs_load") && source.find_chunk(CALLBACK_CHUNK_DATA))
    {
        const char* callback = pSettings->r_string("engine_callbacks", "after_objs_load");

        if (sol::function lua_function; ai().script_engine().function(callback, lua_function))
            lua_function((IReader*)&source);
    }

    can_register_objects(true);

    for (I = B; I != E; ++I)
        (*I).second->on_register();
}

bool CALifeStorageManager::load(LPCSTR save_name)
{
    CTimer timer;
    timer.Start();
    string256 save;
    strcpy_s(save, m_save_name);
    if (!save_name)
    {
        if (!xr_strlen(m_save_name))
            R_ASSERT2(false, "There is no file name specified!");
    }
    else
        strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);

    strcpy_s(m_loaded_save, sizeof(m_loaded_save) - 1, m_save_name);

    string_path file_name;
    std::ignore = FS.update_path(file_name, "$game_saves$", m_save_name);

    IReader* stream;
    stream = FS.r_open(file_name);
    if (!stream)
    {
        Msg("* Cannot find saved game {}", file_name);
        strcpy_s(m_save_name, save);

        return (false);
    }

    R_ASSERT(CSavedGameWrapper::valid_saved_game(*stream), xr::format("{}\nSaved game version mismatch or saved game is corrupted", file_name));

    unload();
    reload(m_section);

    u32 source_count = stream->r_u32();
    void* source_data = xr_malloc(source_count);
    R_ASSERT(zstd::ZSTD_decompress(source_data, source_count, stream->pointer(), stream->length() - 3 * sizeof(u32)) == source_count);

    FS.r_close(stream);
    load(source_data, source_count, file_name);
    xr_free(source_data);

    groups().on_after_game_load();

    VERIFY(graph().actor());

    Msg("* Game {} is successfully loaded from file '{}' ({:.4}s)", save_name, file_name, timer.GetElapsed_sec());

    return (true);
}

void CALifeStorageManager::save(NET_Packet& net_packet)
{
    prepare_objects_for_save();

    shared_str game_name;
    net_packet.r_stringZ(game_name);

    save(game_name.c_str(), !!net_packet.r_u8());
}

void CALifeStorageManager::prepare_objects_for_save()
{
    Level().ClientSend();
    Level().ClientSave();
}
