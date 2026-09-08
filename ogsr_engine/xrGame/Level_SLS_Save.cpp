#include "stdafx.h"

#include "Level.h"

#include "HUDManager.h"
#include "../xr_3da/xrLevel.h"
#include "xrServer.h"

void CLevel::net_Save(LPCSTR name) // Game Save
{
    if (!Server)
    {
        XR_LOG_ERROR("Can't save game on pure client");
        return;
    }

    // 1. Create stream
    CMemoryWriter fs;

    // 2. Description
    fs.open_chunk(fsSLS_Description);
    fs.w_stringZ(net_SessionName());
    fs.close_chunk();

    // 3. Server state
    fs.open_chunk(fsSLS_ServerState);
    Server->SLS_Save(fs);
    fs.close_chunk();

    // Save it to file
    std::ignore = fs.save_to(name);
}
