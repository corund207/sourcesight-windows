#pragma once

#include <string>

// Windows desktop capture: GDI screenshots (BMP) and ffmpeg video (MP4).
// Disable Streamproof to include the overlay in desktop captures.
class ScreenCapture {
public:
    ScreenCapture() = delete;

    // Grab one composited frame (overlay + game) to disk.
    // Empty path -> auto-generated under ./captures/.
    // Returns true on success. The file contains BMP data.
    static bool CaptureScreenshot(const std::string& path = "");

    // Start recording the entire composited screen (overlay + game) to a
    // video file. Empty path -> auto-generated ./captures/*.mp4.
    // Returns false if already recording or no recorder backend is found.
    // This is the "record the entire screen" entry point.
    static bool StartRecording(const std::string& path = "", int fps = 60);

    // Alias with the literal requested name; identical to StartRecording().
    static bool RecordEntireScreen(const std::string& path = "", int fps = 60);

    // Gracefully stop the active recording (send q -> finalize MP4).
    // No-op when not recording.
    static void StopRecording();

    static bool IsRecording();
    static std::string ActiveRecordingPath();

    static std::string DefaultScreenshotPath();
    static std::string DefaultRecordingPath();
};
