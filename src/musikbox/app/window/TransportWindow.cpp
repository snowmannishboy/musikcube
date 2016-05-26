#include "stdafx.h"
#include "TransportWindow.h"

#include <cursespp/Screen.h>
#include <cursespp/Colors.h>
#include <cursespp/Message.h>

#include <app/util/Text.h>

#include <core/debug.h>
#include <core/library/track/LibraryTrack.h>
#include <core/library/LocalLibraryConstants.h>
#include <core/playback/NonLibraryTrackHelper.h>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/chrono.hpp>

#include <algorithm>

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::library;
using namespace musik::core::query;
using namespace musik::core::db;
using namespace musik::box;
using namespace boost::chrono;
using namespace cursespp;

#define REFRESH_TRANSPORT_READOUT 1001
#define REFRESH_INTERVAL_MS 1000

#define DEBOUNCE_REFRESH(x) \
    this->RemoveMessage(REFRESH_TRANSPORT_READOUT); \
    this->PostMessage(REFRESH_TRANSPORT_READOUT, 0, 0, x);

TransportWindow::TransportWindow(LibraryPtr library, Transport& transport)
: Window(NULL) {
    this->SetContentColor(BOX_COLOR_WHITE_ON_BLACK);
    this->SetFrameVisible(false);
    this->library = library;
    this->library->QueryCompleted.connect(this, &TransportWindow::OnQueryCompleted);
    this->transport = &transport;
    this->transport->StreamEvent.connect(this, &TransportWindow::OnTransportStreamEvent);
    this->transport->VolumeChanged.connect(this, &TransportWindow::OnTransportVolumeChanged);
    this->transport->TimeChanged.connect(this, &TransportWindow::OnTransportTimeChanged);
    this->paused = this->focused = false;
}

TransportWindow::~TransportWindow() {
}

void TransportWindow::Show() {
    Window::Show();
    this->Update();
}

void TransportWindow::ProcessMessage(IMessage &message) {
    int type = message.Type();

    if (type == REFRESH_TRANSPORT_READOUT) {
        this->Update();
        DEBOUNCE_REFRESH(REFRESH_INTERVAL_MS)
    }
}

void TransportWindow::OnTransportStreamEvent(int eventType, std::string url) {
    if (eventType == Transport::StreamPlaying) {
        this->trackQuery.reset(new SingleTrackQuery(url));
        this->library->Enqueue(this->trackQuery);
        DEBOUNCE_REFRESH(0);
    }
}

void TransportWindow::OnTransportVolumeChanged() {
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::OnTransportTimeChanged(double time) {
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::OnQueryCompleted(QueryPtr query) {
    if (query == this->trackQuery && query->GetStatus() == QueryBase::Finished) {
        this->currentTrack = this->trackQuery->GetResult();
        DEBOUNCE_REFRESH(0)
    }
}

void TransportWindow::Focus() {
    this->focused = true;
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::Blur() {
    this->focused = false;
    DEBOUNCE_REFRESH(0)
}

void TransportWindow::Update() {
    this->Clear();
    WINDOW *c = this->GetContent();

    bool paused = (transport->GetPlaybackState() == Transport::PlaybackPaused);

    int64 gb = COLOR_PAIR(this->focused
        ? BOX_COLOR_RED_ON_BLACK
        : BOX_COLOR_GREEN_ON_BLACK);

    /* playing SONG TITLE from ALBUM NAME */
    std::string duration = "0";

    if (transport->GetPlaybackState() == Transport::PlaybackStopped) {
        wattron(c, gb);
        wprintw(c, "playback is stopped");
        wattroff(c, gb);
    }
    else {
        std::string title, album;

        if (this->currentTrack) {
            title = this->currentTrack->GetValue(constants::Track::TITLE);
            album = this->currentTrack->GetValue(constants::Track::ALBUM);
            duration = this->currentTrack->GetValue(constants::Track::DURATION);
        }

        title = title.size() ? title : "song title";
        album = album.size() ? album : "album name";
        duration = duration.size() ? duration : "0";

        wprintw(c, "playing ");

        wattron(c, gb);
        wprintw(c, title.c_str());
        wattroff(c, gb);

        wprintw(c, " from ");

        wattron(c, gb);
        wprintw(c, album.c_str());
        wattroff(c, gb);
    }

    /* volume slider */

    wprintw(c, "\n");

    int volumePercent = (size_t) round(this->transport->Volume() * 100.0f) - 1;
    int thumbOffset = std::min(9, (volumePercent * 10) / 100);

    std::string volume = "vol ";

    for (int i = 0; i < 10; i++) {
        volume += (i == thumbOffset) ? "■" : "─";
    }

    volume += "  ";

    wprintw(c, volume.c_str());

    /* time slider */

    int64 timerAttrs = 0;

    if (paused) { /* blink the track if paused */
        int64 now = duration_cast<seconds>(
            system_clock::now().time_since_epoch()).count();

        if (now % 2 == 0) {
            timerAttrs = COLOR_PAIR(BOX_COLOR_BLACK_ON_BLACK);
        }
    }

    transport->Position();

    int secondsCurrent = (int) round(transport->Position());
    int secondsTotal = boost::lexical_cast<int>(duration);

    std::string currentTime = text::Duration(secondsCurrent);
    std::string totalTime = text::Duration(secondsTotal);

    size_t timerWidth =
        this->GetContentWidth() -
        u8len(volume) -
        currentTime.size() -
        totalTime.size() -
        2; /* padding */

    thumbOffset = 0;

    if (secondsTotal) {
        size_t progress = (secondsCurrent * 100) / secondsTotal;
        thumbOffset = std::min(timerWidth - 1, (progress * timerWidth) / 100);
    }

    std::string timerTrack = "";
    for (size_t i = 0; i < timerWidth; i++) {
        timerTrack += (i == thumbOffset) ? "■" : "─";
    }

    wattron(c, timerAttrs); /* blink if paused */
    wprintw(c, currentTime.c_str());
    wattroff(c, timerAttrs);

    wprintw(c, " %s %s",
        timerTrack.c_str(),
        totalTime.c_str());

    this->Repaint();
}
