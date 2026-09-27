#pragma once
#include <cstdint>

// Stable overlay visibility: visible whenever the game or the overlay itself
// holds foreground. Cursor state must not drive Show/Hide.
//
// Root cause of the blink loop: GetCursorInfo is system-global and observes
// our own topmost window. Showing the overlay can flip the snapshot to
// "cursor visible" (arrow over our window / ImGui cursor handling), which hid
// the overlay; hiding it restored the game's hidden raw-input cursor, which
// showed the overlay again. Debounce delays (75/200 ms) only changed the blink
// period. Mouse routing already uses WS_EX_TRANSPARENT | WS_EX_LAYERED plus
// HTTRANSPARENT, which pass input system-wide without hiding, so game cursor
// menus stay clickable while ESP stays visible.
class OverlayVisibility {
public:
    bool Update(bool focused,bool menu_open,bool cursor_visible,std::uint64_t now_ms) {
        // menu_open and cursor_visible are recorded for transition diagnostics
        // only. Focus already covers the SourceSight menu case (the overlay is
        // foreground while open), and cursor snapshots are polluted by the
        // overlay itself, so neither may toggle visibility.
        last_focused_=focused;
        last_menu_open_=menu_open;
        last_cursor_visible_=cursor_visible;
        last_update_ms_=now_ms;
        const bool requested=focused;
        if(requested!=candidate_) {
            candidate_=requested;
            changed_at_=now_ms;
        }
        if(candidate_!=visible_) {
            visible_=candidate_;
            ++transitions_;
            last_transition_ms_=now_ms;
        }
        return visible_;
    }
    void Reset() {
        visible_=true;
        candidate_=true;
        changed_at_=0;
        transitions_=0;
        last_focused_=true;
        last_menu_open_=false;
        last_cursor_visible_=false;
        last_update_ms_=0;
        last_transition_ms_=0;
    }
    bool Visible() const { return visible_; }
    unsigned Transitions() const { return transitions_; }
    bool LastFocused() const { return last_focused_; }
    bool LastMenuOpen() const { return last_menu_open_; }
    bool LastCursorVisible() const { return last_cursor_visible_; }
    std::uint64_t LastUpdateMs() const { return last_update_ms_; }
    std::uint64_t LastTransitionMs() const { return last_transition_ms_; }
private:
    bool visible_=true,candidate_=true;
    std::uint64_t changed_at_=0;
    unsigned transitions_=0;
    bool last_focused_=true,last_menu_open_=false,last_cursor_visible_=false;
    std::uint64_t last_update_ms_=0,last_transition_ms_=0;
};
