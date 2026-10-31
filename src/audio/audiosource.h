#pragma once

#include "audio/audiobuffer.h"

struct AudioSource
{
    QString id;
    QString name;
    QString type;
    QString deviceId;
    QString deviceName;
    float volume = 1;
    bool muted = false;
    AudioPeak peak;
    QString state = QStringLiteral("starting");
    QString error;
};
