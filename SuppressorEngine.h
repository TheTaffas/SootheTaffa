#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <cmath>
#include <algorithm>

// Supresor espectral de resonancias (estilo Soothe).
// FFT 2048 o 4096 (75% overlap, ventana seno). Modos: Stereo (canales linkeados) o
// Mid/Side (analisis independiente). Opcional: la deteccion puede venir del sidechain.
class SuppressorEngine
{
public:
    static constexpr int maxOrder = 12;
    static constexpr int maxN     = 1 << maxOrder;
    static constexpr int numDisplayPoints = 256;

    struct Params
    {
        float depth = 0.6f, sharpness = 0.5f, selectivity = 0.5f;
        float lowHz = 40.f, highHz = 16000.f;
        float attackMs = 10.f, releaseMs = 120.f;
        float maxReductionDb = 12.f;
        float mix = 1.f, outputDb = 0.f;
        bool  delta = false;
        bool  autoGain = false;
        bool  midSide = false;
        bool  useSidechain = false;
        int   order = 12;              // 11 -> 2048 muestras, 12 -> 4096
    };

    int getLatency() const { return lay->N; }

    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        numCh = std::max (1, std::min (2, channels));
        layouts[0].init (11, sr);
        layouts[1].init (12, sr);
        lay = &layouts[1];

        inRing.assign  ((size_t) numCh, std::vector<float> ((size_t) maxN, 0.f));
        outRing.assign ((size_t) numCh, std::vector<float> ((size_t) maxN, 0.f));
        scRing.assign  (2, std::vector<float> ((size_t) maxN, 0.f));
        fftData.assign ((size_t) numCh, std::vector<float> ((size_t) (2 * maxN), 0.f));
        scData.assign  (2, std::vector<float> ((size_t) (2 * maxN), 0.f));
        mdb.assign    (2, std::vector<float> ((size_t) maxN / 2 + 1, -180.f));
        tg.assign     (2, std::vector<float> ((size_t) maxN / 2 + 1, 0.f));
        gstate.assign (2, std::vector<float> ((size_t) maxN / 2 + 1, 0.f));
        cum.assign ((size_t) maxN / 2 + 2, 0.0);
        lastMS = false; msActive = false;
        resetState();

        agCoefE = 1.f - std::exp (-1.f / (0.4f  * (float) sr));
        agCoefG = 1.f - std::exp (-1.f / (0.25f * (float) sr));
        autoGainDb.store (0.f);
        for (int i = 0; i < numDisplayPoints; ++i) { dispLevel[(size_t) i].store (-120.f); dispRed[(size_t) i].store (0.f); }
    }

    void reset() { resetState(); }

    // sidechain: puede ser nullptr (sin sidechain conectado)
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain, const Params& p)
    {
        if (p.order != lay->order)
        {
            lay = &layouts[p.order == 11 ? 0 : 1];
            resetState();
        }
        const bool ms = p.midSide && numCh == 2;
        if (ms != lastMS)
        {
            for (auto& g : gstate) std::fill (g.begin(), g.end(), 0.f);
            lastMS = ms;
        }
        msActive = ms;
        params = p;

        const int N = lay->N, hop = lay->hop;
        const int chans = std::min (numCh, buffer.getNumChannels());
        const int scChans = sidechain != nullptr ? sidechain->getNumChannels() : 0;
        scAvailable = scChans > 0;
        scActive.store (scAvailable && p.useSidechain, std::memory_order_relaxed);

        const int n = buffer.getNumSamples();
        const float outGain = std::pow (10.f, p.outputDb / 20.f);
        const float mix = p.mix;

        for (int s = 0; s < n; ++s)
        {
            float dry[2] = { 0.f, 0.f }, wet[2] = { 0.f, 0.f };
            float d2 = 0.f, w2 = 0.f;

            for (int c = 0; c < numCh; ++c)
            {
                const int src = std::min (c, chans - 1);
                const float in = buffer.getReadPointer (src)[s];
                dry[c] = inRing[(size_t) c][(size_t) pos];     // muestra de hace N (alineada con wet)
                inRing[(size_t) c][(size_t) pos] = in;
                wet[c] = outRing[(size_t) c][(size_t) pos];
                outRing[(size_t) c][(size_t) pos] = 0.f;
                d2 += dry[c] * dry[c];
                w2 += wet[c] * wet[c];
            }
            if (scAvailable)
                for (int c = 0; c < 2; ++c)
                    scRing[(size_t) c][(size_t) pos] = sidechain->getReadPointer (std::min (c, scChans - 1))[s];

            // autogain: iguala la energia del wet a la del dry (solo sube, hasta +12 dB)
            eDry += agCoefE * (d2 / (float) numCh - eDry);
            eWet += agCoefE * (w2 / (float) numCh - eWet);
            float target = 1.f;
            if (eDry > 1e-9f && eWet > 1e-12f)
                target = std::clamp (std::sqrt (eDry / eWet), 1.f, 4.f);
            agGain += agCoefG * (target - agGain);
            const float ag = p.autoGain ? agGain : 1.f;

            if (++pos >= N) pos = 0;
            if (++counter >= hop) { counter = 0; processFrame(); }

            for (int c = 0; c < chans; ++c)
            {
                const float o = p.delta ? (dry[c] - wet[c])
                                        : (dry[c] * (1.f - mix) + wet[c] * ag * mix);
                buffer.getWritePointer (c)[s] = o * outGain;
            }
        }

        autoGainDb.store (20.f * std::log10 (agGain));
    }

    // ---- lectura desde la GUI (hilo de mensajes) ----
    void getDisplay (float* level, float* reduction) const
    {
        for (int i = 0; i < numDisplayPoints; ++i)
        {
            level[i]     = dispLevel[(size_t) i].load (std::memory_order_relaxed);
            reduction[i] = dispRed[(size_t) i].load (std::memory_order_relaxed);
        }
    }
    float getAutoGainDb() const { return autoGainDb.load (std::memory_order_relaxed); }
    bool  isSidechainActive() const { return scActive.load (std::memory_order_relaxed); }

private:
    struct Layout
    {
        int order = 12, N = 4096, hop = 1024, half = 2048;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window;
        std::vector<int> dispLo, dispHi;
        float dispOffset = 62.3f;

        void init (int ord, double sr)
        {
            order = ord; N = 1 << ord; hop = N / 4; half = N / 2;
            fft = std::make_unique<juce::dsp::FFT> (ord);
            window.resize ((size_t) N);
            for (int i = 0; i < N; ++i)
                window[(size_t) i] = std::sin (juce::MathConstants<float>::pi * ((float) i + 0.5f) / (float) N);
            dispOffset = 20.f * std::log10 (0.3183f * (float) N);

            dispLo.assign (numDisplayPoints, 0);
            dispHi.assign (numDisplayPoints, 0);
            const float binHz = (float) (sr / N);
            int prevK = 0;
            for (int i = 0; i < numDisplayPoints; ++i)
            {
                const float f = 20.f * std::pow (1000.f, (float) i / (float) (numDisplayPoints - 1));
                const int k = std::clamp ((int) std::lround (f / binHz), 0, half);
                dispHi[(size_t) i] = k;
                dispLo[(size_t) i] = std::min (k, i == 0 ? k : prevK);
                prevK = k;
            }
        }
    };

    void resetState()
    {
        for (auto& v : inRing)  std::fill (v.begin(), v.end(), 0.f);
        for (auto& v : outRing) std::fill (v.begin(), v.end(), 0.f);
        for (auto& v : scRing)  std::fill (v.begin(), v.end(), 0.f);
        for (auto& g : gstate)  std::fill (g.begin(), g.end(), 0.f);
        eDry = eWet = 0.f; agGain = 1.f;
        pos = 0; counter = 0;
    }

    void processFrame()
    {
        const int N = lay->N, half = lay->half, hop = lay->hop;
        const auto& window = lay->window;
        const bool ms = msActive;
        const bool useSc = scAvailable && params.useSidechain;
        const int nA = ms ? 2 : 1;       // canales de analisis independientes

        const float depth = params.depth;
        const float wfrac = 0.08f + params.selectivity * 0.5f;
        const int   gs    = (int) std::floor ((1.f - params.sharpness) * 8.f + 0.5f);
        const float frameRate = (float) (sampleRate / hop);
        const float atk = std::exp (-1.f / (params.attackMs  * 0.001f * frameRate));
        const float rel = std::exp (-1.f / (params.releaseMs * 0.001f * frameRate));
        const float binHz = (float) (sampleRate / N);
        const float maxRed = params.maxReductionDb;
        constexpr float thr = 2.f;

        // 1) ventana (+ mid/side) + FFT, para la senal principal y, si hace falta, el sidechain
        auto analyse = [&] (std::vector<std::vector<float>>& ring, std::vector<std::vector<float>>& data)
        {
            for (int c = 0; c < numCh; ++c)
            {
                float* d = data[(size_t) c].data();
                const float* in = ring[(size_t) c].data();
                std::fill (d, d + 2 * N, 0.f);
                int idx = pos;
                for (int i = 0; i < N; ++i)
                {
                    d[i] = in[idx] * window[(size_t) i];
                    if (++idx >= N) idx = 0;
                }
            }
            if (ms)
            {
                float* a = data[0].data();
                float* b = data[1].data();
                for (int i = 0; i < N; ++i)
                {
                    const float l = a[i], r = b[i];
                    a[i] = 0.5f * (l + r);
                    b[i] = 0.5f * (l - r);
                }
            }
            for (int c = 0; c < numCh; ++c)
                lay->fft->performRealOnlyForwardTransform (data[(size_t) c].data(), true);
        };

        analyse (inRing, fftData);
        if (useSc) analyse (scRing, scData);
        auto& det = useSc ? scData : fftData;     // de donde se detectan las resonancias

        for (int a = 0; a < nA; ++a)
        {
            float* mdbA = mdb[(size_t) a].data();
            float* tgA  = tg[(size_t) a].data();
            float* gsA  = gstate[(size_t) a].data();

            // 2) magnitud en dB y suma acumulada
            double acc = 0.0;
            cum[0] = 0.0;
            for (int k = 0; k <= half; ++k)
            {
                float pw = 0.f;
                if (nA == 1)
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float re = det[(size_t) c][(size_t) (2 * k)];
                        const float im = det[(size_t) c][(size_t) (2 * k + 1)];
                        pw += re * re + im * im;
                    }
                    pw /= (float) numCh;
                }
                else
                {
                    const float re = det[(size_t) a][(size_t) (2 * k)];
                    const float im = det[(size_t) a][(size_t) (2 * k + 1)];
                    pw = re * re + im * im;
                }
                const float db = 10.f * std::log10 (pw + 1e-18f);
                mdbA[k] = db;
                acc += db;
                cum[(size_t) k + 1] = acc;
            }

            // 3) envolvente suave y deteccion de resonancias
            for (int k = 0; k <= half; ++k)
            {
                const int hw = (int) std::floor (std::max (3.f, (float) k * wfrac));
                const int lo = std::max (0, k - hw);
                const int hi = std::min (half, k + hw);
                const float env = (float) ((cum[(size_t) hi + 1] - cum[(size_t) lo]) / (double) (hi - lo + 1));
                const float ex = mdbA[k] - env - thr;
                float t = 0.f;
                if (ex > 0.f)
                    t = -std::min (maxRed, ex * depth * ex / (ex + 3.f));
                const float f = (float) k * binHz;
                if (f < params.lowHz || f > params.highHz) t = 0.f;
                tgA[k] = t;
            }

            // 4) suavizado en frecuencia (Sharpness)
            acc = 0.0;
            cum[0] = 0.0;
            for (int k = 0; k <= half; ++k) { acc += tgA[k]; cum[(size_t) k + 1] = acc; }

            // 5) suavizado temporal + aplicar ganancia
            for (int k = 0; k <= half; ++k)
            {
                const int lo = std::max (0, k - gs);
                const int hi = std::min (half, k + gs);
                const float gv = (float) ((cum[(size_t) hi + 1] - cum[(size_t) lo]) / (double) (hi - lo + 1));
                float cur = gsA[k];
                cur = (gv < cur) ? gv + (cur - gv) * atk : gv + (cur - gv) * rel;
                gsA[k] = cur;
                const float g = std::pow (10.f, cur / 20.f);
                if (nA == 1)
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        fftData[(size_t) c][(size_t) (2 * k)]     *= g;
                        fftData[(size_t) c][(size_t) (2 * k + 1)] *= g;
                    }
                }
                else
                {
                    fftData[(size_t) a][(size_t) (2 * k)]     *= g;
                    fftData[(size_t) a][(size_t) (2 * k + 1)] *= g;
                }
            }
        }

        // 5b) datos para el display
        for (int i = 0; i < numDisplayPoints; ++i)
        {
            float lv = -200.f, rd = 0.f;
            for (int a = 0; a < nA; ++a)
                for (int k = lay->dispLo[(size_t) i]; k <= lay->dispHi[(size_t) i]; ++k)
                {
                    lv = std::max (lv, mdb[(size_t) a][(size_t) k]);
                    rd = std::min (rd, gstate[(size_t) a][(size_t) k]);
                }
            dispLevel[(size_t) i].store (lv - lay->dispOffset, std::memory_order_relaxed);
            dispRed[(size_t) i].store (rd, std::memory_order_relaxed);
        }

        // 6) IFFT (+ decodificar mid/side) + overlap-add (seno x2 con 75% overlap suma 2 -> 0.5)
        for (int c = 0; c < numCh; ++c)
            lay->fft->performRealOnlyInverseTransform (fftData[(size_t) c].data());
        if (ms)
        {
            float* a = fftData[0].data();
            float* b = fftData[1].data();
            for (int i = 0; i < N; ++i)
            {
                const float m = a[i], s = b[i];
                a[i] = m + s;
                b[i] = m - s;
            }
        }
        for (int c = 0; c < numCh; ++c)
        {
            const float* d = fftData[(size_t) c].data();
            float* out = outRing[(size_t) c].data();
            int idx = pos;
            for (int i = 0; i < N; ++i)
            {
                out[idx] += d[i] * window[(size_t) i] * 0.5f;
                if (++idx >= N) idx = 0;
            }
        }
    }

    std::array<Layout, 2> layouts;
    Layout* lay = &layouts[1];
    double sampleRate = 44100.0;
    int numCh = 2, pos = 0, counter = 0;
    bool lastMS = false, msActive = false, scAvailable = false;
    Params params;
    std::vector<std::vector<float>> mdb, tg, gstate;
    std::vector<double> cum;
    std::vector<std::vector<float>> inRing, outRing, scRing, fftData, scData;

    float eDry = 0.f, eWet = 0.f, agGain = 1.f, agCoefE = 0.f, agCoefG = 0.f;
    std::array<std::atomic<float>, numDisplayPoints> dispLevel, dispRed;
    std::atomic<float> autoGainDb { 0.f };
    std::atomic<bool>  scActive { false };
};
