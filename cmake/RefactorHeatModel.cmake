if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "RefactorHeatModel: INPUT is missing")
endif()

file(READ "${INPUT}" CONTENT)

set(INCLUDE_ANCHOR "#include \"FolderIdentity.h\"\n")
set(INCLUDE_REPLACEMENT "#include \"FolderIdentity.h\"\n#include \"HeatModel.h\"\n")
string(FIND "${CONTENT}" "${INCLUDE_ANCHOR}" INCLUDE_POS)
if(INCLUDE_POS EQUAL -1)
    message(FATAL_ERROR "RefactorHeatModel: include anchor not found")
endif()
string(REPLACE "${INCLUDE_ANCHOR}" "${INCLUDE_REPLACEMENT}" CONTENT "${CONTENT}")

set(START_MARKER "double EffectiveHalfLifeDays(const fhm::Settings& settings) {")
set(END_MARKER "double CombineHeat(double current, double contribution) {")
string(FIND "${CONTENT}" "${START_MARKER}" START_POS)
string(FIND "${CONTENT}" "${END_MARKER}" END_POS)
if(START_POS EQUAL -1 OR END_POS EQUAL -1 OR END_POS LESS_EQUAL START_POS)
    message(FATAL_ERROR "RefactorHeatModel: heat math block anchors not found")
endif()

string(SUBSTRING "${CONTENT}" 0 ${START_POS} BEFORE)
string(SUBSTRING "${CONTENT}" ${END_POS} -1 AFTER)

set(MODEL_BLOCK [=[
const fhm::DualTimescaleActivityModel g_heatModel;

double EffectiveHalfLifeDays(const fhm::Settings& settings) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    fhm::CoolingInput input{};
    input.automatic = settings.coolingAuto;
    input.configuredHalfLifeDays = settings.coolingHalfLifeDays;
    input.activeDaysIn60 = settings.coolingAuto ? g_readDatabase.GetRecentActiveDays(now, 60) : 0;
    return g_heatModel.EffectiveHalfLifeDays(input);
}

fhm::FolderHeatInput FolderHeatModelInput(const fhm::StoredActivity& activity, double halfLifeDays) {
    fhm::FolderHeatInput input{};
    input.visits = activity.visits;
    input.recentVisits = activity.recentVisits;
    input.activeDays = activity.activeDays;
    const std::int64_t today = CurrentDayKey();
    const std::int64_t first = activity.firstActiveDay > 0 ? activity.firstActiveDay : today;
    input.spanDays = static_cast<double>(std::max<std::int64_t>(1, today - first + 1));
    input.daysSinceLastEffectiveVisit = DaysAgo(activity.lastEffectiveVisit);
    input.halfLifeDays = halfLifeDays;
    input.hasLastEffectiveVisit = FileTimeTicks(activity.lastEffectiveVisit) != 0;
    return input;
}

double DirectHeat(const fhm::StoredActivity& activity, double halfLifeDays) {
    return g_heatModel.CalculateFolderHeat(FolderHeatModelInput(activity, halfLifeDays));
}

fhm::FileHeatInput FileHeatModelInput(const fhm::StoredFileActivity& activity, double halfLifeDays) {
    fhm::FileHeatInput input{};
    input.writeEvents = activity.writeEvents;
    input.activeDays = activity.activeDays;
    const std::int64_t today = CurrentDayKey();
    const std::int64_t first = activity.firstActiveDay > 0 ? activity.firstActiveDay : today;
    input.spanDays = static_cast<double>(std::max<std::int64_t>(1, today - first + 1));
    input.daysSinceLastWrite = DaysAgo(activity.lastWrite);
    input.halfLifeDays = halfLifeDays;
    input.hasLastWrite = FileTimeTicks(activity.lastWrite) != 0;
    return input;
}

double FileHeat(const fhm::StoredFileActivity& activity, double halfLifeDays) {
    return g_heatModel.CalculateFileHeat(FileHeatModelInput(activity, halfLifeDays));
}

]=])

set(CONTENT "${BEFORE}${MODEL_BLOCK}${AFTER}")
file(WRITE "${INPUT}" "${CONTENT}")
message(STATUS "RefactorHeatModel: generated engine now uses DualTimescaleActivityModel")
