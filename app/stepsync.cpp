#include <RGBWWCtrl.h>
#include <algorithm>

StepSync::~StepSync() {
};

uint32_t ClockCatchUp::reset() {
    _firstMasterSync = true;
    _catchupOffset = 0;
    _steering = 1.0;
    return _constBaseInt;
}

uint32_t ClockCatchUp::onMasterClock(uint32_t stepsCurrent, uint32_t stepsMaster) {
    uint32_t nextInt = _constBaseInt;
    if (!_firstMasterSync) {
        int diff = StepSync::calcOverflowVal(_stepsSyncLast, stepsCurrent);
        int masterDiff = StepSync::calcOverflowVal(_stepsSyncMasterLast, stepsMaster);

        int curOffset = masterDiff - diff;
        _catchupOffset += curOffset;
        debug_d("Diff: %d | Master Diff: %d | CurOffset: %d | Catchup Offset: %d\n", diff, masterDiff, curOffset, _catchupOffset);

        float curSteering = 1.0 - static_cast<float>(_catchupOffset) / masterDiff;
        curSteering = std::min(std::max(curSteering, 0.5f), 1.5f);
        _steering = 0.5f *_steering + 0.5f * curSteering;
        nextInt *= _steering;
        debug_d("New Int: %d | CurSteering: %f | Steering: %f\n", nextInt, curSteering, _steering);
    }

    _stepsSyncMasterLast = stepsMaster;
    _stepsSyncLast = stepsCurrent;
    _firstMasterSync = false;

    return nextInt;
}

int ClockCatchUp::getCatchupOffset() const {
    return _catchupOffset;
}

