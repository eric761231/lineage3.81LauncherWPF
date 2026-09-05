// NumberingMarkerHook.h: repurposes the "Action_NumberingMarker" hotbar
// button (icon 5582) from its stock target-select behavior into a party
// leader attack-target-mark toggle, matching the server's already-existing
// C_SendLocation type=50 (PARTY_ATTACK_TARGET_MARK) handler. See
// docs/hooks/NUMBERING_MARKER_TOGGLE_BRIEF.md (LinBin3.81 project) for the
// full reverse-engineering brief this is based on.
#pragma once

// Currently a no-op: InstallNumberingMarkerHook logs and returns so the
// stock Action_NumberingMarker (0x62EC90) runs. Call site remains in
// DelayedDetourThread. Re-enable DetourAttach in the .cpp if the toggle
// product comes back.
void InstallNumberingMarkerHook();
