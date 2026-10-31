/*
    SPDX-FileCopyrightText: 2026 Rishon Jonathan R
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtTextToSpeech

// Reads Set Up's instructions aloud.
Item {
    id: voice

    readonly property bool available: tts.state !== TextToSpeech.Error && tts.engine !== ""

    function say(text) {
        // Queued: interrupting overlaps with speech-dispatcher, which stops asynchronously.
        if (available) {
            tts.enqueue(text);
        }
    }

    function stop() {
        tts.stop();
    }

    TextToSpeech {
        id: tts
        // Fall back to flite when speech-dispatcher isn't running
        engine: "speechd"
        onStateChanged: {
            if (state === TextToSpeech.Error && engine === "speechd" && availableEngines().includes("flite")) {
                engine = "flite";
            }
        }
    }
}
