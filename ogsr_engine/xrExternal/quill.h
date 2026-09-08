#ifndef __XREXTERNAL_QUILL_H
#define __XREXTERNAL_QUILL_H

#include "../xrCore/defines.h"

#define FMTQUILL_UNICODE 1

#define QUILL_DETAILED_FUNCTION_NAME
#define QUILL_DISABLE_NON_PREFIXED_MACROS
#define QUILL_ENABLE_IMMEDIATE_FLUSH 0

#ifndef _DEBUG
#define QUILL_COMPILE_ACTIVE_LOG_LEVEL QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO
#endif

#include <quill/LogMacros.h>

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wnewline-eof");
XR_DIAG_IGNORE("-Wnrvo");

#include <quill/Logger.h>

XR_DIAG_POP();

#include <quill/backend/BackendOptions.h>

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wnan-infinity-disabled");

#include <quill/std/FilesystemPath.h>

XR_DIAG_POP();

#include <quill/std/SystemError.h>

QUILL_BEGIN_NAMESPACE
QUILL_BEGIN_EXPORT

class ManualBackendWorker;

QUILL_END_EXPORT
QUILL_END_NAMESPACE

namespace xr
{
class quill_manual_backend final
{
private:
    quill::ManualBackendWorker* worker;

public:
    constexpr quill_manual_backend& operator=(quill_manual_backend&&) = delete;

    void init(quill::BackendOptions options);
    void poll(std::chrono::microseconds timeout) const;
    void shutdown();
};
} // namespace xr

#endif // !__XREXTERNAL_QUILL_H
