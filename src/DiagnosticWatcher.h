#pragma once

#include <atomic>
#include <string>

namespace fhm {
class EngineLogger;
namespace runtime { struct SharedState; }

using RemovalCallback = void(*)(const std::wstring& path);

void RunDeletionDiagnostics(runtime::SharedState* shared, EngineLogger* log,
                            std::atomic<bool>* stopping, RemovalCallback onRemoved);

} // namespace fhm
