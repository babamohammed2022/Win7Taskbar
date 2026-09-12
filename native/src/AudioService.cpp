/*
 * Win7Taskbar - Core nativo - Volume di sistema
 */

#include "AudioService.h"
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <algorithm>

namespace w7t {
namespace {

/* Piccolo aiuto per inizializzare COM solo quando serve davvero e
 * rilasciarlo in modo sicuro anche in caso di uscita anticipata. */
class ComScope {
public:
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        /* RPC_E_CHANGED_MODE significa che COM e' gia' inizializzato in un
         * altro modello: possiamo lavorare lo stesso, ma non dobbiamo
         * chiamare CoUninitialize. */
        m_owned = SUCCEEDED(hr);
    }

    ~ComScope() {
        if (m_owned) {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool m_owned = false;
};

/* Ottiene l'oggetto volume dell'uscita audio predefinita.
 * Il chiamante deve invocare Release() sul risultato. */
IAudioEndpointVolume* OpenEndpointVolume() {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) {
        return nullptr;
    }

    IMMDevice* device = nullptr;
    /* eRender + eMultimedia = l'uscita predefinita per la riproduzione,
     * cioe' quella che il cursore del volume controlla. */
    const HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumerator->Release();

    if (FAILED(hr) || device == nullptr) {
        return nullptr;
    }

    IAudioEndpointVolume* volume = nullptr;
    device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                     nullptr, reinterpret_cast<void**>(&volume));
    device->Release();

    return volume;
}

} /* namespace */

int32_t AudioService::GetVolume(int32_t* outLevel, int32_t* outMuted) {
    ComScope com;

    IAudioEndpointVolume* volume = OpenEndpointVolume();
    if (volume == nullptr) {
        /* Nessuna scheda audio (o audio disabilitato): non e' un errore
         * fatale, il chiamante mostrera' il cursore a zero. */
        return W7T_ERR_NOT_FOUND;
    }

    if (outLevel != nullptr) {
        float scalar = 0.0f;
        if (SUCCEEDED(volume->GetMasterVolumeLevelScalar(&scalar))) {
            /* Il valore e' 0..1 in scala percettiva: la stessa che usa il
             * cursore di Windows, quindi basta moltiplicare per 100. */
            *outLevel = static_cast<int32_t>((scalar * 100.0f) + 0.5f);
        } else {
            *outLevel = 0;
        }
    }

    if (outMuted != nullptr) {
        BOOL muted = FALSE;
        *outMuted = SUCCEEDED(volume->GetMute(&muted)) && muted ? 1 : 0;
    }

    volume->Release();
    return W7T_OK;
}

int32_t AudioService::SetVolume(int32_t level) {
    ComScope com;

    IAudioEndpointVolume* volume = OpenEndpointVolume();
    if (volume == nullptr) {
        return W7T_ERR_NOT_FOUND;
    }

    const int32_t clamped = (std::max)(0, (std::min)(100, level));
    const float scalar = static_cast<float>(clamped) / 100.0f;

    const HRESULT hr = volume->SetMasterVolumeLevelScalar(scalar, nullptr);

    /* Spostare il cursore sopra lo zero riattiva l'audio, come fa Windows. */
    if (SUCCEEDED(hr) && clamped > 0) {
        BOOL muted = FALSE;
        if (SUCCEEDED(volume->GetMute(&muted)) && muted) {
            volume->SetMute(FALSE, nullptr);
        }
    }

    volume->Release();
    return SUCCEEDED(hr) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

int32_t AudioService::SetMuted(bool muted) {
    ComScope com;

    IAudioEndpointVolume* volume = OpenEndpointVolume();
    if (volume == nullptr) {
        return W7T_ERR_NOT_FOUND;
    }

    const HRESULT hr = volume->SetMute(muted ? TRUE : FALSE, nullptr);
    volume->Release();

    return SUCCEEDED(hr) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

} /* namespace w7t */
