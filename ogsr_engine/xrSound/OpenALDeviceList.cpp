/*
 * Copyright (c) 2005, Creative Labs Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and
 * 	     the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 * 	     and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of Creative Labs Inc. nor the names of its contributors may be used to endorse or
 * 	     promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "stdafx.h"

#include "OpenALDeviceList.h"

// #include <objbase.h>

ALDeviceList::ALDeviceList()
{
    // snd_device_id = u32(-1);

    Enumerate();
}

/*
 * Exit call
 */
ALDeviceList::~ALDeviceList()
{
    for (int i = 0; snd_devices_token[i].name; i++)
    {
        auto ptr = const_cast<gsl::zstring>(snd_devices_token[i].name);
        xr_free(ptr);
    }

    xr_free(snd_devices_token);
    snd_devices_token = nullptr;
}

void ALDeviceList::IterateDevicesList(const char* devices, bool enumerateAllPresent)
{
    // go through device list (each device terminated with a single NULL, list terminated with double NULL)
    while (*devices)
    {
        if (ALCdevice* device = alcOpenDevice(devices))
        {
            if (ALCcontext* context = alcCreateContext(device, nullptr))
            {
                // alcMakeContextCurrent(context); // оно тут вроде б как не нужно, а ломает рестарт налету

                const bool is_al_soft = strstr(devices, AL_SOFT);

                // if new actual device name isn't already in the list, then add it...
                pcstr actualDeviceName = alcGetString(device, is_al_soft && enumerateAllPresent ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);

                if (actualDeviceName != nullptr && xr_strlen(actualDeviceName) > 0)
                {
                    int major, minor;

                    alcGetIntegerv(device, ALC_MAJOR_VERSION, sizeof(int), &major);
                    alcGetIntegerv(device, ALC_MINOR_VERSION, sizeof(int), &minor);

                    m_devices.emplace_back(actualDeviceName, minor, major, is_al_soft);
                }
                alcDestroyContext(context);
            }
            alcCloseDevice(device);
        }
        devices += xr_strlen(devices) + 1;
    }
}

void ALDeviceList::Enumerate()
{
    XR_LOG_INFO("OpenAL: enumerate devices...");

    // have a set of vectors storing the device list, selection status, spec version #, and XRAM support status
    m_devices.clear();

    // CoUninitialize(); // ???

    // grab function pointers for 1.0-API functions, and if successful proceed to enumerate all devices

    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
    {
        const char* devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);

        IterateDevicesList(devices, true);

        xr_strcpy(m_defaultDeviceName, alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER));
    }
    else if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT"))
    {
        const char* devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);

        IterateDevicesList(devices, false);

        xr_strcpy(m_defaultDeviceName, alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER));
    }
    else
    {
        XR_LOG_ERROR("OpenAL: EnumerationExtension NOT Present");
    }

    u32 _cnt = GetNumDevices();

    const char* prefix = "OpenAL Soft on "; // to just cut off this long prefix in token names
    bool need_to_trim_prefix = true;

    for (u32 i = 0; i < _cnt; ++i)
    {
        char* str = m_devices[i].name;

        if (!strstr(str, prefix))
        {
            need_to_trim_prefix = false;
            break;
        }
    }

    // make token
    snd_devices_token = xr_alloc<xr_token>(_cnt + 1);

    snd_devices_token[_cnt].id = -1;
    snd_devices_token[_cnt].name = nullptr;

    for (u32 i = 0; i < _cnt; ++i)
    {
        char* str = m_devices[i].name;

        if (need_to_trim_prefix && strstr(str, prefix))
            str += xr_strlen(prefix);

        snd_devices_token[i].id = i;
        snd_devices_token[i].name = xr_strdup(str);
    }

    if (need_to_trim_prefix && strstr(m_defaultDeviceName, prefix))
        xr_strcpy(m_defaultDeviceName, m_defaultDeviceName + xr_strlen(prefix));

    XR_LOG_INFO("OpenAL: Default sound device name is [{}], device name size: [{}]", m_defaultDeviceName, xr_strlen(m_defaultDeviceName));

    if (0 != GetNumDevices())
    {
        XR_LOG_INFO("OpenAL: All available devices:");

        for (u32 j = 0; j < GetNumDevices(); j++)
        {
            ALDeviceDesc al_device_desc = GetDeviceDesc(j);

            XR_LOG_INFO(" {}. {} (full name [{}]). al_soft [{}]", j + 1, snd_devices_token[j].name, al_device_desc.name, al_device_desc.is_al_soft);
        }
    }
    else
    {
        XR_LOG_ERROR("OpenAL: No devices available");
    }

    // CoInitializeEx(NULL, COINIT_MULTITHREADED); // ???
}

void ALDeviceList::SelectBestDeviceId(const char* system_default_device) const
{
    if (GetNumDevices() == 0)
    {
        XR_LOG_ERROR("Can't select device. List empty");

        snd_device_id = u32(-1);
        return;
    }

    if (snd_device_id == u32(-1) || snd_device_id >= GetNumDevices())
    {
        XR_ASSERT(GetNumDevices() != 0);

        // select best
        u32 new_device_id = 0; // first

        for (int i = 0; snd_devices_token[i].name; i++)
        {
            // check openAL default device first
            if (m_defaultDeviceName[0] != '\0' && std::is_eq(xr::strcasecmp(m_defaultDeviceName, snd_devices_token[i].name)))
            {
                new_device_id = i;
                break;
            }

            // check OS system default device too
            if (system_default_device != nullptr && std::is_eq(xr::strcasecmp(system_default_device, snd_devices_token[i].name)))
            {
                new_device_id = i;
                break;
            }
        }

        snd_device_id = new_device_id;
    }

    XR_LOG_NOTICE("Selected device is [{}]", snd_devices_token[snd_device_id].name);
}

/*
 * Returns the major and minor version numbers for a device at a specified index in the complete list
 */
void ALDeviceList::GetDeviceVersion(u32 index, int* major, int* minor) const
{
    *major = m_devices[index].major_ver;
    *minor = m_devices[index].minor_ver;
}
