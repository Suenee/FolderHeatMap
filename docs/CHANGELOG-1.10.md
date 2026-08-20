# FolderHeatMap 1.10 - 20.08.2026

- Coalesces shared-RAM publication: batches and own-directory snapshots that are equivalent to the currently published state no longer create a new cache generation.
- Suppresses duplicate same-directory navigation notifications before they can enqueue SLOW work or predictions.
- Removes the second scheduling of the same hot-child predictions from the SLOW refresh path; navigation remains the authoritative prediction trigger.
- Persists the durable runtime cache only when the public RAM state actually changed. Failed persistence marks the cache dirty again for a later retry.
- Uses a 0.001 Heat equivalence tolerance so tiny time-decay differences do not create pointless cache generations while visible Heat levels/steps, Visits, Writes and timestamps still remain exact change triggers.
- Coalesces final shutdown publication and persistence instead of blindly publishing unchanged ready batches.
- Leaves Heat mathematics, File Heat semantics, Total Commander color/icon behavior, logging location, WDX hot path and FAST/SLOW architecture unchanged.
- The optimization is generated from the verified `src/EngineApp.cpp` into a build-only source by `cmake/GenerateOptimizedEngine.cmake`. Every source transformation has a guarded exact anchor; CMake fails rather than building if the underlying stable engine source no longer matches the expected 1.09 implementation.
