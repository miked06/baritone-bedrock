#include "core/Security.h"

#include <sddl.h>

OpenSecurity::OpenSecurity() {
    // Generic all for authenticated users, low mandatory label (no write-up restriction).
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;AU)S:(ML;;NW;;;LW)", SDDL_REVISION_1,
                                                         &descriptor_, nullptr);
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
}

OpenSecurity::~OpenSecurity() {
    if (descriptor_) LocalFree(descriptor_);
}
