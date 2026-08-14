#include "MultibandReverb/SpectrumAnalyzer.h"
#include "MultibandReverb/PluginProcessor.h"

// ---------------------------------------------------------------------------
// Shared visual constants (must match across paint / hit test / drag).
// ---------------------------------------------------------------------------
static constexpr float VOL_MIN_DB    = -60.0f;
static constexpr float VOL_MAX_DB    =  12.0f;
static constexpr float TRANS_OCTAVES =  0.8f;
static constexpr float SPEC_MIN_DB   = -90.0f;
static constexpr float SPEC_MAX_DB   =  12.0f;

static float volDbToNorm(float db) {
    return juce::jmap(juce::jlimit(VOL_MIN_DB, VOL_MAX_DB, db),
                      VOL_MIN_DB, VOL_MAX_DB, 0.05f, 0.85f);
}

static const juce::Colour BAND_COLOURS[] = {
    juce::Colour(0xff6ec6f5), // light blue  (band 0)
    juce::Colour(0xfff5a623), // orange      (band 1)
    juce::Colour(0xff7ed321), // green       (band 2)
    juce::Colour(0xffbd10e0), // purple      (band 3)
    juce::Colour(0xffe91e63), // pink        (band 4)
    juce::Colour(0xff00bcd4), // cyan        (band 5)
    juce::Colour(0xffffeb3b), // yellow      (band 6)
    juce::Colour(0xff4caf50), // mid green   (band 7)
};

// Crossover handle colours match the band below each crossover.
static juce::Colour crossoverHandleColour(int crossoverIndex) {
    // crossover i sits between band i and band i+1; use band i's colour.
    return BAND_COLOURS[static_cast<size_t>(crossoverIndex) % 8];
}

// ---------------------------------------------------------------------------
SpectrumAnalyzer::SpectrumAnalyzer()
    : fft(FFT_ORDER),
      window(FFT_SIZE, juce::dsp::WindowingFunction<float>::hann) {
    // Allocate all large buffers on the heap to avoid Rosetta layout issues.
    outFifo   .assign(FFT_SIZE, 0.0f);
    outFftData.assign(FFT_SIZE, 0.0f);
    outIn     .assign(FFT_SIZE, 0.0f);
    outDisp   .assign(FFT_SIZE, 0.0f);
    inFifo    .assign(FFT_SIZE, 0.0f);
    inFftData .assign(FFT_SIZE, 0.0f);
    inIn      .assign(FFT_SIZE, 0.0f);
    inDisp    .assign(FFT_SIZE, 0.0f);
    historyFrames.assign(HISTORY_FRAMES, std::vector<float>(FFT_SIZE, 0.0f));
    startTimerHz(60);
    setOpaque(true);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { stopTimer(); }

void SpectrumAnalyzer::setCrossoverFrequencies(const std::vector<float> &freqs) {
    juce::CriticalSection::ScopedLockType lock(crossoverMutex);
    crossoverFreqs = freqs;
}

float SpectrumAnalyzer::getFrequencyForX(float x) const {
    const float fw = static_cast<float>(getWidth());
    return std::exp(std::log(20.0f) + (std::log(20000.0f) - std::log(20.0f)) * x / fw);
}

float SpectrumAnalyzer::getXForFrequency(float freq) const {
    const float fw = static_cast<float>(getWidth());
    return fw * (std::log(freq) - std::log(20.0f))
              / (std::log(20000.0f) - std::log(20.0f));
}

int SpectrumAnalyzer::hitTestCrossover(float x) const {
    for (int i = 0; i < (int)crossoverFreqs.size(); ++i)
        if (std::abs(x - getXForFrequency(crossoverFreqs[static_cast<size_t>(i)])) < 8.0f)
            return i;
    return -1;
}

// Catmull-Rom cubic interpolation between FFT bins for smooth display.
float SpectrumAnalyzer::getBinValue(const std::vector<float> &data, float freq) const {
    const float exact = freq * FFT_SIZE / static_cast<float>(sampleRate);
    const int   b0    = juce::jlimit(0, FFT_SIZE / 2 - 1, static_cast<int>(exact));
    const int   b1    = juce::jmin(b0 + 1, FFT_SIZE / 2 - 1);
    const int   b2    = juce::jmin(b0 + 2, FFT_SIZE / 2 - 1);
    const int   bm1   = juce::jmax(b0 - 1, 0);
    const float t     = exact - static_cast<float>(b0);

    const float p0 = data[static_cast<size_t>(bm1)];
    const float p1 = data[static_cast<size_t>(b0)];
    const float p2 = data[static_cast<size_t>(b1)];
    const float p3 = data[static_cast<size_t>(b2)];

    return juce::jmax(0.0f,
        0.5f * ((2.0f * p1)
            + (-p0 + p2) * t
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t));
}

// Returns a colour for a frequency, blended between adjacent band colours
// at crossover transitions using the same sigmoid used by the band curves.
juce::Colour SpectrumAnalyzer::getBandColourForFrequency(
    float freq, const std::vector<float> &bandBounds) const
{
    const int numBands = static_cast<int>(bandBounds.size()) - 1;
    if (numBands <= 0) return BAND_COLOURS[0];

    // Find which band this frequency primarily belongs to.
    int band = numBands - 1;
    for (int i = 0; i < numBands - 1; ++i) {
        if (freq < bandBounds[static_cast<size_t>(i + 1)]) { band = i; break; }
    }

    const juce::Colour col = BAND_COLOURS[static_cast<size_t>(band) % 8];

    // Blend toward the next band's colour within the crossover transition zone.
    if (band < numBands - 1) {
        const float crossFreq = bandBounds[static_cast<size_t>(band + 1)];
        const float crossLo   = crossFreq / std::exp2(TRANS_OCTAVES * 0.5f);
        const float crossHi   = crossFreq * std::exp2(TRANS_OCTAVES * 0.5f);
        if (freq > crossLo && freq < crossHi) {
            const float t     = (std::log(freq) - std::log(crossLo))
                              / (std::log(crossHi) - std::log(crossLo));
            const float blend = 1.0f / (1.0f + std::exp(-(t - 0.5f) * 10.0f));
            const juce::Colour nextCol = BAND_COLOURS[static_cast<size_t>(band + 1) % 8];
            return col.interpolatedWith(nextCol, blend);
        }
    }
    return col;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void SpectrumAnalyzer::paint(juce::Graphics &g) {
    const auto bounds = getLocalBounds();
    const int  w = bounds.getWidth();
    const int  h = bounds.getHeight();
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);

    // =========================================================
    // Background
    // =========================================================
    g.fillAll(juce::Colour(0xff0a0a1a));

    juce::ColourGradient radial(
        juce::Colour(0xff1a2a5a).withAlpha(0.6f), fw * 0.3f, fh * 0.5f,
        juce::Colour(0xff0a0a1a).withAlpha(0.0f), fw * 0.9f, fh * 0.5f,
        true);
    g.setGradientFill(radial);
    g.fillRect(bounds);

    // Watermark.
    {
        g.saveState();
        g.setFont(juce::Font(juce::FontOptions(fh * 0.28f).withStyle("Bold Italic")));
        g.setColour(juce::Colours::white.withAlpha(0.028f));
        g.drawText("MultibandReverb", bounds.reduced(20, 0),
                   juce::Justification::centredRight, false);
        g.restoreState();
    }

    // Fixed-seed grain texture.
    {
        juce::Random rng(42);
        g.setColour(juce::Colours::white.withAlpha(0.018f));
        for (int i = 0; i < (w * h) / 80; ++i)
            g.fillRect(rng.nextInt(w), rng.nextInt(h), 1, 1);
    }

    // =========================================================
    // dB and frequency grids
    // =========================================================
    g.setFont(juce::Font(juce::FontOptions(10.0f)));

    const int dbLevels[] = { 0, -12, -24, -36, -48, -60 };
    for (int level : dbLevels) {
        const float norm = juce::jmap(static_cast<float>(level),
                                      SPEC_MIN_DB, SPEC_MAX_DB, 1.0f, 0.0f);
        const float y = fh * norm;
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, fw);
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawText(juce::String(level) + "dB", w - 38, static_cast<int>(y) - 8,
                   34, 14, juce::Justification::right);
    }

    const int freqMarkers[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (int freq : freqMarkers) {
        const float x = getXForFrequency(static_cast<float>(freq));
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, fh);
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        const juce::String label = freq >= 1000
            ? (juce::String(freq / 1000) + "k") : juce::String(freq);
        g.drawText(label, static_cast<int>(x) - 16, h - 15, 32, 13,
                   juce::Justification::centred);
    }

    // =========================================================
    // Build band bounds for colour mapping (needs crossover lock)
    // =========================================================
    std::vector<float> bandBounds;
    {
        juce::CriticalSection::ScopedLockType lock(crossoverMutex);
        bandBounds.push_back(20.0f);
        for (float f : crossoverFreqs) bandBounds.push_back(f);
        bandBounds.push_back(20000.0f);
    }

    // =========================================================
    // Ghost trail: older output frames, falling and fading
    // =========================================================
    // We draw oldest-first (so newest paints on top).
    // historyHead points to the NEXT write slot, so the most recent
    // valid frame is at (historyHead - 1 + HISTORY_FRAMES) % HISTORY_FRAMES.
    {
        const int validFrames = juce::jmin(historyCount, HISTORY_FRAMES);
        for (int age = validFrames - 1; age >= 1; --age) {
            const int slot = (historyHead - 1 - age + HISTORY_FRAMES * 2) % HISTORY_FRAMES;
            const auto &frame = historyFrames[static_cast<size_t>(slot)];

            // Older = lower alpha, shifted down more, stroke slightly wider (faux blur).
            const float alpha  = std::pow(HISTORY_FADE, static_cast<float>(age));
            const float dropY  = static_cast<float>(age * HISTORY_DROP);
            const float strokeW = 0.8f + static_cast<float>(age) * 0.18f;

            // Build a clipping path so the ghost fades to nothing at the bottom.
            // We tint each pixel column with the band colour.
            juce::Path ghostPath;
            bool started = false;

            for (int px = 0; px < w; px += 2) {
                const float freq  = getFrequencyForX(static_cast<float>(px));
                const float level = getBinValue(frame, freq);
                const float db    = juce::Decibels::gainToDecibels(level, SPEC_MIN_DB);
                const float norm  = juce::jmap(db, SPEC_MIN_DB, SPEC_MAX_DB, 0.0f, 0.72f);
                const float y     = fh * (1.0f - norm) + dropY;

                if (!started) { ghostPath.startNewSubPath(static_cast<float>(px), y); started = true; }
                else          { ghostPath.lineTo(static_cast<float>(px), y); }
            }

            // Draw as a single translucent white stroke (colour comes from fill below).
            g.setColour(juce::Colours::white.withAlpha(alpha * 0.35f));
            g.strokePath(ghostPath, juce::PathStrokeType(strokeW));

            // Add a grain dot pass at the ghost's opacity for texture.
            {
                juce::Random rng(static_cast<juce::int64>(age * 1337));
                g.setColour(juce::Colours::white.withAlpha(alpha * 0.012f));
                for (int i = 0; i < (w * h) / 200; ++i)
                    g.fillRect(rng.nextInt(w), rng.nextInt(h), 1, 1);
            }
        }
    }

    // =========================================================
    // Input (dry) spectrum: dim blue, subtle, behind output
    // =========================================================
    {
        juce::Path inPath;
        inPath.startNewSubPath(0.0f, fh);
        for (int px = 0; px < w; ++px) {
            const float freq  = getFrequencyForX(static_cast<float>(px));
            const float level = getBinValue(inDisp, freq);
            const float db    = juce::Decibels::gainToDecibels(level, SPEC_MIN_DB);
            const float norm  = juce::jmap(db, SPEC_MIN_DB, SPEC_MAX_DB, 0.0f, 0.72f);
            inPath.lineTo(static_cast<float>(px), fh * (1.0f - norm));
        }
        inPath.lineTo(fw, fh);
        inPath.closeSubPath();

        // Very dim fill and faint stroke — this reads as "before".
        g.setColour(juce::Colour(0xff1a3a5a).withAlpha(0.35f));
        g.fillPath(inPath);
        g.setColour(juce::Colour(0xff4a7aaa).withAlpha(0.4f));
        g.strokePath(inPath, juce::PathStrokeType(0.8f));
    }

    // =========================================================
    // Output (wet) spectrum: per-band coloured, bright, on top
    // =========================================================
    {
        // We draw the output spectrum as a series of per-pixel vertical slices
        // coloured by band, then stroke the top edge separately per segment.
        // For efficiency we batch consecutive pixels of the same colour into paths.

        // First pass: build one big filled path using gradient fill per segment.
        // Simple approach: draw the whole filled path in white, then tint with
        // a per-band horizontal gradient. This avoids N separate path builds.

        // Build the top-edge polyline for the output spectrum.
        std::vector<juce::Point<float>> outEdge;
        outEdge.reserve(static_cast<size_t>(w));
        for (int px = 0; px < w; ++px) {
            const float freq  = getFrequencyForX(static_cast<float>(px));
            const float level = getBinValue(outDisp, freq);
            const float db    = juce::Decibels::gainToDecibels(level, SPEC_MIN_DB);
            const float norm  = juce::jmap(db, SPEC_MIN_DB, SPEC_MAX_DB, 0.0f, 0.72f);
            outEdge.push_back({ static_cast<float>(px), fh * (1.0f - norm) });
        }

        // Draw per-band coloured segments.
        // For each band, clip horizontally to its frequency range and draw
        // the filled path in that band's colour. Blending at crossovers is
        // handled by the colour interpolation in getBandColourForFrequency.
        const int numBands = static_cast<int>(bandBounds.size()) - 1;

        // Draw each band's filled region separately with its gradient.
        for (int band = 0; band < numBands; ++band) {
            const float leftFreq  = bandBounds[static_cast<size_t>(band)];
            const float rightFreq = bandBounds[static_cast<size_t>(band + 1)];

            // Extend slightly past crossover for smooth overlap blending.
            const float drawLeftFreq  = (band == 0) ? 20.0f
                : leftFreq / std::exp2(TRANS_OCTAVES * 0.3f);
            const float drawRightFreq = (band == numBands - 1) ? 20000.0f
                : rightFreq * std::exp2(TRANS_OCTAVES * 0.3f);

            const float x0 = getXForFrequency(drawLeftFreq);
            const float x1 = getXForFrequency(drawRightFreq);

            const juce::Colour col     = BAND_COLOURS[static_cast<size_t>(band) % 8];
            const juce::Colour colNext = BAND_COLOURS[static_cast<size_t>(band + 1) % 8];

            // Build path clipped to [x0, x1].
            const int pxStart = juce::jmax(0, static_cast<int>(x0));
            const int pxEnd   = juce::jmin(w - 1, static_cast<int>(x1));

            if (pxStart >= pxEnd) continue;

            juce::Path seg;
            seg.startNewSubPath(static_cast<float>(pxStart), fh);
            for (int px = pxStart; px <= pxEnd; ++px)
                seg.lineTo(outEdge[static_cast<size_t>(px)]);
            seg.lineTo(static_cast<float>(pxEnd), fh);
            seg.closeSubPath();

            // Gradient from this band's colour (left) to next (right) for smooth blend.
            juce::ColourGradient hGrad(
                col.withAlpha(0.55f),     static_cast<float>(pxStart), 0.0f,
                (band == numBands - 1 ? col : colNext).withAlpha(0.45f),
                static_cast<float>(pxEnd), 0.0f, false);
            g.setGradientFill(hGrad);
            g.fillPath(seg);
        }

        // Single bright stroke on top edge of output, per-band coloured.
        // We draw short line segments coloured by frequency.
        for (int px = 1; px < w; ++px) {
            const float freq = getFrequencyForX(static_cast<float>(px));
            const juce::Colour col = getBandColourForFrequency(freq, bandBounds);
            g.setColour(col.withAlpha(0.9f));
            g.drawLine(outEdge[static_cast<size_t>(px - 1)].x,
                       outEdge[static_cast<size_t>(px - 1)].y,
                       outEdge[static_cast<size_t>(px)].x,
                       outEdge[static_cast<size_t>(px)].y,
                       1.6f);
        }

        // Grain texture inside the output spectrum fill.
        {
            juce::Random rng(99);
            for (int i = 0; i < (w * h) / 60; ++i) {
                const int gx = rng.nextInt(w);
                const int gy = rng.nextInt(h);
                const float freq  = getFrequencyForX(static_cast<float>(gx));
                const float level = getBinValue(outDisp, freq);
                const float db    = juce::Decibels::gainToDecibels(level, SPEC_MIN_DB);
                const float norm  = juce::jmap(db, SPEC_MIN_DB, SPEC_MAX_DB, 0.0f, 0.72f);
                const float topY  = fh * (1.0f - norm);
                if (static_cast<float>(gy) > topY) {
                    const juce::Colour col = getBandColourForFrequency(freq, bandBounds);
                    // Fade grain toward the bottom.
                    const float fade = juce::jmap(static_cast<float>(gy),
                                                  topY, fh, 0.07f, 0.0f);
                    g.setColour(col.withAlpha(fade));
                    g.fillRect(gx, gy, 1, 1);
                }
            }
        }
    }

    // =========================================================
    // Per-band volume curves + labels
    // =========================================================
    if (audioProcessor != nullptr) {
        juce::CriticalSection::ScopedLockType lock(crossoverMutex);

        const int numBands = audioProcessor->getNumBands();
        const float mouseX = static_cast<float>(getMouseXYRelative().x);
        const float mouseY = static_cast<float>(getMouseXYRelative().y);

        auto sigmoid = [](float t) -> float {
            return 1.0f / (1.0f + std::exp(-(t - 0.5f) * 10.0f));
        };

        for (int band = 0; band < numBands && band < static_cast<int>(bandBounds.size()) - 1; ++band) {
            const float leftFreq  = bandBounds[static_cast<size_t>(band)];
            const float rightFreq = bandBounds[static_cast<size_t>(band + 1)];
            const float volDb     = audioProcessor->getBandVolumeDb(band);
            const float flatY     = fh * (1.0f - volDbToNorm(volDb));
            const float botY      = fh * 0.99f;

            // Clamp transition width so it never exceeds half the band's log width.
            // This prevents the hump that appears when bands are narrow.
            const float bandLogWidth = std::log2(rightFreq / leftFreq);
            const float safeOctaves  = juce::jmin(TRANS_OCTAVES, bandLogWidth * 0.45f);

            const float leftLo  = leftFreq  / std::exp2(safeOctaves * 0.5f);
            const float leftHi  = leftFreq  * std::exp2(safeOctaves * 0.5f);
            const float rightLo = rightFreq / std::exp2(safeOctaves * 0.5f);
            const float rightHi = rightFreq * std::exp2(safeOctaves * 0.5f);

            juce::Path strokePath;
            bool strokeStarted = false;

            for (int px = 0; px <= w; ++px) {
                const float x    = static_cast<float>(px);
                const float freq = getFrequencyForX(x);
                float y          = botY;

                if (freq >= leftLo && freq <= rightHi) {
                    if (freq < leftHi) {
                        const float t = (std::log(freq) - std::log(leftLo))
                                      / (std::log(leftHi) - std::log(leftLo));
                        y = juce::jmap(sigmoid(t), 0.0f, 1.0f, botY, flatY);
                    } else if (freq <= rightLo) {
                        y = flatY;
                    } else {
                        const float t = (std::log(freq) - std::log(rightLo))
                                      / (std::log(rightHi) - std::log(rightLo));
                        y = juce::jmap(sigmoid(t), 0.0f, 1.0f, flatY, botY);
                    }
                }

                if (y < botY * 0.99f) {
                    if (!strokeStarted) { strokePath.startNewSubPath(x, y); strokeStarted = true; }
                    else                { strokePath.lineTo(x, y); }
                } else if (strokeStarted) {
                    strokePath.lineTo(x, y);
                    strokeStarted = false;
                }
            }

            const juce::Colour col     = BAND_COLOURS[static_cast<size_t>(band) % 8];
            const bool         isDrag  = (draggedVolumeBand == band);

            g.setColour(col.withAlpha(isDrag ? 1.0f : 0.75f));
            g.strokePath(strokePath, juce::PathStrokeType(isDrag ? 2.6f : 1.8f));

            // Hover / drag overlays.
            const float flatLeftX  = getXForFrequency(leftFreq  * std::exp2(TRANS_OCTAVES * 0.5f));
            const float flatRightX = getXForFrequency(rightFreq / std::exp2(TRANS_OCTAVES * 0.5f));
            const float midX       = (flatLeftX + flatRightX) * 0.5f;
            const bool  hoverFlat  = (mouseX >= flatLeftX && mouseX <= flatRightX
                                   && std::abs(mouseY - flatY) < 20.0f);

            if (isDrag || hoverFlat) {
                juce::Path dash;
                for (float dx = flatLeftX; dx < flatRightX; dx += 10.0f) {
                    dash.startNewSubPath(dx, flatY);
                    dash.lineTo(juce::jmin(dx + 6.0f, flatRightX), flatY);
                }
                g.setColour(col.withAlpha(0.7f));
                g.strokePath(dash, juce::PathStrokeType(1.0f));

                g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
                g.setColour(juce::Colours::white.withAlpha(0.95f));
                g.drawText(juce::String(volDb, 1) + " dB",
                           static_cast<int>(midX) - 30,
                           static_cast<int>(flatY) - 15,
                           60, 12, juce::Justification::centred);
            }

            // IR name below line.
            const juce::String irName = audioProcessor->getBandIRName(band);
            if (irName.isNotEmpty() && (flatRightX - flatLeftX) > 40.0f) {
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.setColour(col.withAlpha(0.5f));
                g.drawText(irName,
                           static_cast<int>(flatLeftX) + 4,
                           static_cast<int>(flatY) + 4,
                           static_cast<int>(flatRightX - flatLeftX) - 8, 11,
                           juce::Justification::centred, true);
            }
        }
    }

    // =========================================================
    // Crossover handles
    // =========================================================
    {
        juce::CriticalSection::ScopedLockType lock(crossoverMutex);
        const float mouseX = static_cast<float>(getMouseXYRelative().x);
        const float mouseY = static_cast<float>(getMouseXYRelative().y);

        for (int i = 0; i < (int)crossoverFreqs.size(); ++i) {
            const float cx     = getXForFrequency(crossoverFreqs[static_cast<size_t>(i)]);
            const bool hovered = std::abs(mouseX - cx) < 8.0f || (draggedIndex == i);
            const juce::Colour col = crossoverHandleColour(i);

            g.setColour(col.withAlpha(hovered ? 0.85f : 0.5f));
            g.drawVerticalLine(static_cast<int>(cx), 0.0f, fh);

            const float hw = hovered ? 12.0f : 8.0f;
            g.setColour(col.withAlpha(hovered ? 1.0f : 0.7f));
            g.fillRoundedRectangle(cx - hw * 0.5f, 2.0f, hw, 18.0f, 3.0f);

            const float freq = crossoverFreqs[static_cast<size_t>(i)];
            const juce::String fl = freq >= 1000.0f
                ? (juce::String(freq / 1000.0f, 1) + " kHz")
                : (juce::String(static_cast<int>(freq)) + " Hz");
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.setColour(juce::Colours::white.withAlpha(hovered ? 1.0f : 0.75f));
            g.drawText(fl, static_cast<int>(cx) - 30, 22, 60, 13,
                       juce::Justification::centred);
        }

        const int crossHit = hitTestCrossover(mouseX);
        const int volHit   = hitTestVolumeLine(mouseX, mouseY);
        if (crossHit >= 0 || draggedIndex >= 0)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (volHit >= 0 || draggedVolumeBand >= 0)
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ---------------------------------------------------------------------------
void SpectrumAnalyzer::mouseDown(const juce::MouseEvent &e) {
    const float mx = static_cast<float>(e.x);
    const float my = static_cast<float>(e.y);
    {
        juce::CriticalSection::ScopedLockType lock(crossoverMutex);
        draggedIndex = hitTestCrossover(mx);
    }
    if (draggedIndex >= 0) return;
    draggedVolumeBand = hitTestVolumeLine(mx, my);
    lastDragY         = my;
}

void SpectrumAnalyzer::mouseDrag(const juce::MouseEvent &e) {
    const float mx = static_cast<float>(e.x);
    const float my = static_cast<float>(e.y);

    if (draggedIndex >= 0 && audioProcessor != nullptr) {
        audioProcessor->setCrossoverFrequency(draggedIndex,
            juce::jlimit(20.0f, 20000.0f, getFrequencyForX(mx)));
        repaint(); return;
    }

    if (draggedVolumeBand >= 0 && audioProcessor != nullptr) {
        const float dbRange = VOL_MAX_DB - VOL_MIN_DB;
        const float dbDelta = (lastDragY - my) * (dbRange / (static_cast<float>(getHeight()) * 0.80f));
        lastDragY = my;
        const float newDb = juce::jlimit(VOL_MIN_DB, VOL_MAX_DB,
            audioProcessor->getBandVolumeDb(draggedVolumeBand) + dbDelta);
        if (auto *p = audioProcessor->parameters.getParameter(
                "band" + juce::String(draggedVolumeBand) + "_vol"))
            p->setValueNotifyingHost(p->convertTo0to1(newDb));
        repaint();
    }
}

void SpectrumAnalyzer::mouseUp(const juce::MouseEvent &) {
    draggedIndex = -1; draggedVolumeBand = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SpectrumAnalyzer::mouseMove(const juce::MouseEvent &) { repaint(); }

// ---------------------------------------------------------------------------
int SpectrumAnalyzer::hitTestVolumeLine(float mx, float my) const {
    if (!audioProcessor) return -1;
    juce::CriticalSection::ScopedLockType lock(
        const_cast<juce::CriticalSection &>(crossoverMutex));

    const int numBands = audioProcessor->getNumBands();
    std::vector<float> bb;
    bb.push_back(20.0f);
    for (float f : crossoverFreqs) bb.push_back(f);
    bb.push_back(20000.0f);

    for (int band = 0; band < numBands && band < (int)bb.size() - 1; ++band) {
        const float leftFreq  = bb[static_cast<size_t>(band)];
        const float rightFreq = bb[static_cast<size_t>(band + 1)];

        // Drag zone spans the full band frequency range horizontally and the
        // full analyzer height vertically. Click anywhere in the band column
        // and drag up/down to adjust volume.
        const float zoneLeftX  = getXForFrequency(leftFreq);
        const float zoneRightX = getXForFrequency(rightFreq);
        if (mx < zoneLeftX || mx > zoneRightX) continue;

        // Exclude the top ~24px where crossover handles live.
        if (my < 24.0f) continue;

        return band;
    }
    return -1;
}

// ---------------------------------------------------------------------------
void SpectrumAnalyzer::timerCallback() {
    bool needRepaint = false;

    // Process output FFT: compute magnitude, store in outIn.
    if (outFftReady) {
        window.multiplyWithWindowingTable(outFftData.data(), FFT_SIZE);
        fft.performFrequencyOnlyForwardTransform(outFftData.data());

        for (size_t i = 0; i < FFT_SIZE / 2; ++i)
            outIn[i] = outFftData[i];

        // Push raw frame into history ring buffer for ghost trails.
        historyFrames[static_cast<size_t>(historyHead)] = outIn;
        historyHead  = (historyHead + 1) % HISTORY_FRAMES;
        historyCount = juce::jmin(historyCount + 1, HISTORY_FRAMES);

        outFftReady = false;
        needRepaint = true;
    }

    // Process input FFT.
    if (inFftReady) {
        window.multiplyWithWindowingTable(inFftData.data(), FFT_SIZE);
        fft.performFrequencyOnlyForwardTransform(inFftData.data());

        for (size_t i = 0; i < FFT_SIZE / 2; ++i)
            inIn[i] = inFftData[i];

        inFftReady  = false;
        needRepaint = true;
    }

    // Decay display arrays toward the current raw values.
    // outDisp can only fall by decayDbPerTick dB per tick, never below outIn.
    // This is the GMPI approach: dbs_disp[i] = max(dbs_in[i], dbs_disp[i] - dbDecay)
    // We work in gain domain: convert decay to a gain multiplier.
    const float decayGainMult = juce::Decibels::decibelsToGain(-decayDbPerTick);
    for (size_t i = 0; i < FFT_SIZE / 2; ++i) {
        outDisp[i] = std::max(outIn[i],  outDisp[i] * decayGainMult);
        inDisp[i]  = std::max(inIn[i],   inDisp[i]  * decayGainMult);
    }

    if (needRepaint) repaint();
}

void SpectrumAnalyzer::pushBuffer(const float *data, int size) {
    for (int i = 0; i < size; ++i) {
        outFifo[static_cast<size_t>(outFifoIndex)] = data[i];
        if (++outFifoIndex >= FFT_SIZE) {
            std::copy(outFifo.begin(), outFifo.end(), outFftData.begin());
            outFftReady  = true;
            outFifoIndex = 0;
        }
    }
}

void SpectrumAnalyzer::pushInputBuffer(const float *data, int size) {
    for (int i = 0; i < size; ++i) {
        inFifo[static_cast<size_t>(inFifoIndex)] = data[i];
        if (++inFifoIndex >= FFT_SIZE) {
            std::copy(inFifo.begin(), inFifo.end(), inFftData.begin());
            inFftReady  = true;
            inFifoIndex = 0;
        }
    }
}

void SpectrumAnalyzer::resized() {}
