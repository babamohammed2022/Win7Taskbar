/*
 * Win7Taskbar - Core nativo - Volume di sistema
 *
 * Accesso al volume del dispositivo audio predefinito tramite Core Audio
 * (IMMDeviceEnumerator / IAudioEndpointVolume), le stesse API usate dal
 * cursore del volume di Windows.
 */

#ifndef WIN7TASKBAR_AUDIOSERVICE_H
#define WIN7TASKBAR_AUDIOSERVICE_H

#include "Common.h"

namespace w7t {

class AudioService {
public:
    /**
     * Legge il volume principale.
     *
     * @param outLevel  livello 0..100 (puo' essere nullo)
     * @param outMuted  1 se disattivato, 0 altrimenti (puo' essere nullo)
     * @return W7T_OK, oppure W7T_ERR_NOT_FOUND se non c'e' un dispositivo
     *         audio utilizzabile.
     */
    static int32_t GetVolume(int32_t* outLevel, int32_t* outMuted);

    /** Imposta il volume principale (0..100). */
    static int32_t SetVolume(int32_t level);

    /** Attiva o disattiva l'audio. */
    static int32_t SetMuted(bool muted);
};

} /* namespace w7t */

#endif /* WIN7TASKBAR_AUDIOSERVICE_H */
