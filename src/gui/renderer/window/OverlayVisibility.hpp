#pragma once
#include <cstdint>

class OverlayVisibility {
public:
    bool Update(bool focused,bool menu_open,bool cursor_visible,std::uint64_t now_ms) {
        if(!focused || menu_open) {
            visible_=focused;
            candidate_=visible_;
            changed_at_=now_ms;
            return visible_;
        }
        const bool requested=!cursor_visible;
        if(requested!=candidate_) {
            candidate_=requested;
            changed_at_=now_ms;
        }
        // Ignore brief cursor changes; resume only after stable capture.
        const std::uint64_t delay=requested?200:75;
        if(candidate_!=visible_ && now_ms-changed_at_>=delay)visible_=candidate_;
        return visible_;
    }
private:
    bool visible_=true,candidate_=true;
    std::uint64_t changed_at_=0;
};
