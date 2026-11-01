#ifndef AUDIOSOURCE_H
#define AUDIOSOURCE_H

#include "audio/audiobuffer.h"
#include <QMetaType>

enum class AudioSourceType {
    Microphone,
    SystemAudio
};
Q_DECLARE_METATYPE(AudioSourceType)

struct AudioSource
{
    QString id;
    QString name;
    AudioSourceType type = AudioSourceType::Microphone;
    QString deviceId;
    QString deviceName;
    float volume = 1;
    bool muted = false;
    AudioPeak peak;
    QString state = QStringLiteral("starting");
    QString error;
};

#endif
