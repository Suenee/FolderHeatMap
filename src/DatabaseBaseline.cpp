#include "Database.h"

namespace fhm {

bool Database::ObserveFileWriteBaselineSafe(const FolderIdentity& identity, const FILETIME& lastWrite) {
    const auto current = GetFileActivity(identity);
    if (!current) return ResetDirectActivity(identity, false, &lastWrite);
    return ObserveFileWrite(identity, lastWrite);
}

} // namespace fhm
