// =============================================================================
// TD Engine - BeatSystem implementation
// File: src/ecs/beat_system.cpp
//
// Implements the BPM-synced metronome described in docs/RHYTHM_MECHANICS.md.
// Design notes:
//
//  - Per-frame: if engineTime >= nextBeatTime, fire beat event + advance
//    nextBeatTime += spb. Recompute upperBound/lowerBound as two half-windows.
//
//  - On-beat window is TWO half-ranges (not one symmetric window) so the
//    forward-looking half stays reachable after nextBeatTime advances.
//      upperBound = lastBeatTime + windowHalf   (after the beat fired)
//      lowerBound = nextBeatTime - windowHalf   (before the next beat)
//    isOnBeat = (engineTime >= upperBound_from_prev) ||
//               (engineTime <= lowerBound_to_next)
//
//  - Loop detection: if engineTime goes backward (song looped), hard-reset
//    nextBeatTime = startTime + floor((engineTime - startTime) / spb) * spb.
//    This avoids drift accumulation across loops (the bug the video's author
//    hit and fixed by hand).
// =============================================================================

#include "system.h"
#include "world.h"
#include "../core/logger.h"
#include <cmath>
#include <cstring>

namespace td {

void BeatSystem::update(World* world, float /*dt*/) {
    if (!world) return;

    // Engine time source: use the callback if set; otherwise fall back to
    // accumulating dt. (The WASM bridge sets the time source to td::g_time.)
    float engineTime = 0.0f;
    if (m_timeSource) {
        engineTime = m_timeSource();
    }

    // Query all entities with a BeatTrackerComponent.
    EntityId ents[256];
    ComponentMask mask = componentBit(ComponentType::BeatTracker);
    int n = world->queryActive(mask, ents, 256);

    for (int i = 0; i < n; i++) {
        BeatTrackerComponent* bt = world->getComponent<BeatTrackerComponent>(ents[i]);
        if (!bt || !bt->active) continue;

        // Loop detection: engine time went backward (song looped, or tracker
        // was just reset). Recompute nextBeatTime relative to startTime so
        // we don't drift.
        if (engineTime < bt->lastBeatTime) {
            float elapsed = engineTime - bt->startTime;
            if (elapsed < 0) elapsed = 0;
            int beatsElapsed = (int)std::floor(elapsed / bt->spb);
            bt->nextBeatTime = bt->startTime + (beatsElapsed + 1) * bt->spb;
            bt->lastBeatTime = bt->startTime + beatsElapsed * bt->spb;
            bt->beatCount = beatsElapsed;
            // Update bounds for the new beat window.
            bt->upperBound = bt->lastBeatTime + bt->windowHalf;
            bt->lowerBound = bt->nextBeatTime - bt->windowHalf;
        }

        // Fire beat ticks for any beats we've crossed since last frame.
        // (Catch-up loop: handles the case where dt > spb and multiple beats
        // should have fired. Caps at 16 iterations to avoid infinite loops
        // if spb is set to something tiny by mistake.)
        int safety = 16;
        while (engineTime >= bt->nextBeatTime && safety-- > 0) {
            bt->lastBeatTime = bt->nextBeatTime;
            bt->nextBeatTime += bt->spb;
            bt->beatCount++;

            // Two-half-window bounds: upperBound looks forward from the beat
            // that JUST fired (so the player has windowHalf seconds AFTER
            // the beat to still count as "on beat"); lowerBound looks
            // backward from the NEXT beat (so the player has windowHalf
            // seconds BEFORE the next beat to count as "on beat").
            bt->upperBound = bt->lastBeatTime + bt->windowHalf;
            bt->lowerBound = bt->nextBeatTime - bt->windowHalf;

            // Fire the callback if registered.
            if (m_callback) {
                m_callback(bt->beatCount, bt->lastBeatTime);
            }
        }
    }
}

bool BeatSystem::isOnBeat(const BeatTrackerComponent& tracker, float engineTime) const {
    if (!tracker.active) return false;

    // Two-half-window trick from the source video. The on-beat window is
    // actually the union of two adjacent half-windows around each beat:
    //
    //   [lastBeatTime - windowHalf, lastBeatTime + windowHalf]   <-- upper half
    //   [nextBeatTime - windowHalf, nextBeatTime + windowHalf]   <-- lower half
    //
    // But since lastBeatTime + windowHalf and nextBeatTime - windowHalf are
    // adjacent (spb is generally >= 2*windowHalf), this reads to the player
    // as one continuous window straddling each beat.
    //
    // We test:
    //   (engineTime >= lastBeatTime - windowHalf) && (engineTime <= upperBound)
    //   OR
    //   (engineTime >= lowerBound) && (engineTime <= nextBeatTime + windowHalf)
    //
    // Simplified: if we're within windowHalf of EITHER lastBeatTime or
    // nextBeatTime, we're on beat.
    float distFromLast = std::fabs(engineTime - tracker.lastBeatTime);
    float distFromNext = std::fabs(engineTime - tracker.nextBeatTime);
    return distFromLast <= tracker.windowHalf || distFromNext <= tracker.windowHalf;
}

} // namespace td
