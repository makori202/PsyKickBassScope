#include "PluginProcessor.h"
#include "PluginEditor.h"

PsyKickBassScopeProcessor::PsyKickBassScopeProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain (Kick)", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PsyKickBassScopeProcessor::prepareToPlay(double sampleRate, int) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    lp1L=lp2L=lp3L=lp4L=lp1R=lp2R=lp3R=lp4R=0.0;
    ap1xL=ap1yL=ap2xL=ap2yL=ap1xR=ap1yR=ap2xR=ap2yR=0.0;
    dlyBufL.fill(0.0); dlyBufR.fill(0.0); dlyWrite = 0;
    scopeKick.fill(0.0f); scopeBass.fill(0.0f); scopeEnv.fill(1.0f);
    subEnv = topEnv = 1.0;
}

void PsyKickBassScopeProcessor::releaseResources() {}

bool PsyKickBassScopeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    auto sc = layouts.getChannelSet(true, 1);
    if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void PsyKickBassScopeProcessor::setParamsFromJson(const juce::String& jsonStr) {
    auto v = juce::JSON::parse(jsonStr);
    if (!v.isObject()) return;
    pPhaseDeg.store((float)v.getProperty("phase", 0.0));
    pDelayMs.store((float)v.getProperty("delayMs", 0.0));
    pInvert.store((float)v.getProperty("inv", 0.0));
    pSubDuck.store((float)v.getProperty("subDuck", 88.0) * 0.01f);
    pTopDuck.store((float)v.getProperty("topDuck", 65.0) * 0.01f);
    pHoldMs.store((float)v.getProperty("hold", 18.0));
    pRelMs.store((float)v.getProperty("rel", 65.0));
    pCurve.store((float)v.getProperty("curve", 60.0) * 0.01f);
    pXoverHz.store((float)v.getProperty("xover", 120.0));
    pWarmth.store((float)v.getProperty("warmth", 25.0) * 0.01f);
    pMono.store((float)v.getProperty("mono", 1.0));
    pSubGainDb.store((float)v.getProperty("subGain", 0.0));
    pM30x.store((float)v.getProperty("m30x", 0.0));
}

juce::String PsyKickBassScopeProcessor::getScopeJson() {
    juce::String k = "[", b = "[", e = "[";
    for (int i = 0; i < SCOPE_SIZE; ++i) {
        k += juce::String(scopeKick[(size_t)i], 3) + (i < SCOPE_SIZE - 1 ? "," : "");
        b += juce::String(scopeBass[(size_t)i], 3) + (i < SCOPE_SIZE - 1 ? "," : "");
        e += juce::String(scopeEnv[(size_t)i], 3)  + (i < SCOPE_SIZE - 1 ? "," : "");
    }
    k += "]"; b += "]"; e += "]";
    return "{\"sc\":" + juce::String(hasSidechain.load() ? 1 : 0) + ",\"k\":" + k + ",\"b\":" + b + ",\"e\":" + e + "}";
}

void PsyKickBassScopeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    const int n = buffer.getNumSamples();
    auto mainBus = getBusBuffer(buffer, true, 0);
    auto outBus  = getBusBuffer(buffer, false, 0);
    if (mainBus.getNumChannels() < 2 || outBus.getNumChannels() < 2) return;

    auto* L = outBus.getWritePointer(0);
    auto* R = outBus.getWritePointer(1);
    const auto* inLP = mainBus.getReadPointer(0);
    const auto* inRP = mainBus.getReadPointer(1);

    const float* scLP = nullptr;
    const float* scRP = nullptr;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) {
        auto scBus = getBusBuffer(buffer, true, 1);
        if (scBus.getNumChannels() >= 2) {
            scLP = scBus.getReadPointer(0);
            scRP = scBus.getReadPointer(1);
        }
    }

    double bpm = 140.0;
    bool dawPlaying = false;
    double ppqStart = 0.0;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (pos->getBpm().hasValue() && *pos->getBpm() > 20.0) bpm = *pos->getBpm();
            dawPlaying = pos->getIsPlaying();
            if (dawPlaying && pos->getPpqPosition().hasValue()) ppqStart = *pos->getPpqPosition();
        }
    }

    const float phaseDeg = pPhaseDeg.load();
    const double phi = phaseDeg * 3.14159265358979 / 180.0;
    const double aAp = (phaseDeg < 1.0f) ? 0.0 : juce::jlimit(-0.96, 0.96, std::tan(juce::jlimit(0.001, 1.56, phi * 0.25)) * 0.65);
    const double delaySamps = juce::jlimit(0.0, 2000.0, (double)pDelayMs.load() * 0.001 * sr);
    const int dInt = (int)std::floor(delaySamps);
    const double dFrac = delaySamps - (double)dInt;
    const double pol = (pInvert.load() > 0.5f) ? -1.0 : 1.0;

    const double fc = juce::jlimit(50.0, 300.0, (double)pXoverHz.load());
    const double g = 1.0 - std::exp(-2.0 * 3.14159265358979 * fc / sr);
    const double subDuck = pSubDuck.load(), topDuck = pTopDuck.load();
    const double holdMs = pHoldMs.load(), relMs = juce::jmax(5.0f, pRelMs.load());
    const double pwr = 0.6 + pCurve.load() * 2.6;
    const double warmth = pWarmth.load();
    const bool monoOn = pMono.load() > 0.5f;
    const double subGain = std::pow(10.0, juce::jlimit(-6.0, 6.0, (double)pSubGainDb.load()) / 20.0) * (pM30x.load() > 0.5f ? 1.585 : 1.0);

    const double bps = bpm / 60.0;
    const double beatMs = 1000.0 / bps;
    const double smAtt = 1.0 - std::exp(-1.0 / (0.0006 * sr));
    const double smRel = 1.0 - std::exp(-1.0 / (0.0020 * sr));

    float maxSc = 0.0f;
    for (int i = 0; i < n; ++i) {
        double rawL = inLP[i], rawR = inRP[i];

        dlyBufL[(size_t)dlyWrite] = rawL;
        dlyBufR[(size_t)dlyWrite] = rawR;
        int r1 = (dlyWrite - dInt + DELAY_BUF) % DELAY_BUF;
        int r2 = (dlyWrite - dInt - 1 + DELAY_BUF) % DELAY_BUF;
        double inL = dlyBufL[(size_t)r1] * (1.0 - dFrac) + dlyBufL[(size_t)r2] * dFrac;
        double inR = dlyBufR[(size_t)r1] * (1.0 - dFrac) + dlyBufR[(size_t)r2] * dFrac;
        dlyWrite = (dlyWrite + 1) % DELAY_BUF;

        double ap1L = -aAp * inL + ap1xL + aAp * ap1yL; ap1xL = inL; ap1yL = ap1L;
        double ap2L = -aAp * ap1L + ap2xL + aAp * ap2yL; ap2xL = ap1L; ap2yL = ap2L;
        double ap1R = -aAp * inR + ap1xR + aAp * ap1yR; ap1xR = inR; ap1yR = ap1R;
        double ap2R = -aAp * ap1R + ap2xR + aAp * ap2yR; ap2xR = ap1R; ap2yR = ap2R;
        double sigL = ap2L * pol, sigR = ap2R * pol;

        lp1L += g * (sigL - lp1L); lp2L += g * (lp1L - lp2L); lp3L += g * (lp2L - lp3L); lp4L += g * (lp3L - lp4L);
        lp1R += g * (sigR - lp1R); lp2R += g * (lp1R - lp2R); lp3R += g * (lp2R - lp3R); lp4R += g * (lp3R - lp4R);
        double lowL = lp4L, highL = sigL - lowL;
        double lowR = lp4R, highR = sigR - lowR;

        double bp = dawPlaying ? std::fmod(std::abs(ppqStart + i * bps / sr), 1.0) : std::fmod(beatPhase + bps / sr, 1.0);
        beatPhase = bp;
        double posMs = bp * beatMs;

        double tgtSub = 1.0, tgtTop = 1.0;
        if (posMs < holdMs) {
            tgtSub = 1.0 - subDuck;
            tgtTop = 1.0 - topDuck;
        } else {
            double rt = juce::jlimit(0.0, 1.0, (posMs - holdMs) / relMs);
            tgtSub = (1.0 - subDuck) + subDuck * std::pow(rt, pwr);
            double rTop = juce::jlimit(0.0, 1.0, (posMs - holdMs * 0.7) / juce::jmax(5.0, relMs * 0.65));
            tgtTop = (1.0 - topDuck) + topDuck * std::pow(rTop, pwr);
        }

        subEnv += ((tgtSub < subEnv) ? smAtt : smRel) * (tgtSub - subEnv);
        topEnv += ((tgtTop < topEnv) ? smAtt : smRel) * (tgtTop - topEnv);

        lowL *= subEnv; lowR *= subEnv;
        highL *= topEnv; highR *= topEnv;

        if (monoOn) {
            double mid = 0.5 * (lowL + lowR);
            lowL = lowR = mid;
        }
        if (warmth > 0.001) {
            double drv = 1.0 + warmth * 5.0;
            double sL = std::tanh(lowL * drv) / std::tanh(drv);
            double sR = std::tanh(lowR * drv) / std::tanh(drv);
            lowL = lowL * (1.0 - warmth) + sL * warmth;
            lowR = lowR * (1.0 - warmth) + sR * warmth;
        }

        float outL = (float)std::tanh(juce::jlimit(-1.5, 1.5, lowL * subGain + highL));
        float outR = (float)std::tanh(juce::jlimit(-1.5, 1.5, lowR * subGain + highR));
        L[i] = outL; R[i] = outR;

        // Beat-locked oscilloscope bin (0..127 across exactly 1 beat = Kick + Bass aligned!)
        int bin = juce::jlimit(0, SCOPE_SIZE - 1, (int)(bp * (double)SCOPE_SIZE));
        float kickMono = (scLP && scRP) ? 0.5f * (scLP[i] + scRP[i]) : 0.0f;
        if (std::abs(kickMono) > maxSc) maxSc = std::abs(kickMono);
        scopeKick[(size_t)bin] = kickMono;
        scopeBass[(size_t)bin] = 0.5f * (outL + outR);
        scopeEnv[(size_t)bin]  = (float)subEnv;
    }
    if (maxSc > 0.001f) hasSidechain.store(true);
}

juce::AudioProcessorEditor* PsyKickBassScopeProcessor::createEditor() { return new PsyKickBassScopeEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PsyKickBassScopeProcessor(); }
