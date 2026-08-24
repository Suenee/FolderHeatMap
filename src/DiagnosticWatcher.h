#pragma once

#include <atomic>

namespace fhm {
class EngineLogger;
namespace runtime { struct SharedState; }

void RunDeletionDiagnostics(runtime::SharedState* shared, EngineLogger* log, std::atomic<bool>* stopping);

} // namespace fhm
