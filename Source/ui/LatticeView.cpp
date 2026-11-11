#include "LatticeView.h"

namespace tonnetz::ui
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

        // The key's compact band (§3.6): for a major key, F C G D on the tonic's row and A E B
        // above, as offsets from the tonic; a minor key's is its relative major's.
        constexpr std::array<Vec, 7> bandOffsets { { { -1, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } } };
        constexpr Vec relativeMajor { 1, -1 };

        // The torus's period vectors: the copies of a shape.
        Vec copyOffset (int i, int j) { return torusPeriodA * i + torusPeriodB * j; }
    }

    LatticeView::LatticeView (TonnetzProcessor& p, const LatticeModel& m, EditorNotes& n)
        : processor (p), model (m), notes (n),
          follow (p.editorView(), {}, [this]
          {
              auto& v = processor.editorView();
              v.follow = ! v.follow;
              if (v.follow)
                  followChords (false);
              repaint();
          }, 11.5f),
          release (p.editorView(), "release", [this] { releaseLattice(); repaint(); }, 11.5f)
    {
        setOpaque (true);
        addAndMakeVisible (follow);
        addAndMakeVisible (release);
        targetA = p.editorView().centreA;
        targetB = p.editorView().centreB;
    }

    double LatticeView::nodeRadius() const { return juce::jlimit (8.5, 26.0, geo.spacing() * 0.19); }
    double LatticeView::chipRadius() const { return juce::jlimit (8.0, 16.0, geo.spacing() * 0.15); }

    void LatticeView::updateGeometry()
    {
        const auto& v = processor.editorView();
        Camera c;
        c.a = v.centreA;
        c.b = v.centreB;
        c.spacing = juce::jlimit (26.0, 220.0, LatticeGeometry::defaultSpacing (getWidth(), getHeight()) * v.zoom);
        geo = LatticeGeometry (0.0, 0.0, getWidth(), getHeight(), c);
    }

    void LatticeView::resized()
    {
        updateGeometry();
        const int y = getHeight() - 20;
        follow.setBounds (getWidth() - 74, y, 70, 18);
        release.setBounds (getWidth() - 74 - 62, y, 58, 18);
    }

    //==========================================================================
    juce::String LatticeView::nodeLabel (Vec node) const
    {
        // Literal spelling from an anchor: the tonic nearest the camera when there's a key, else
        // the node nearest the camera as a major tonic (Lattice.h, re-anchored as it pans).
        const Vec centre { (int) std::lround (geo.camera().a), (int) std::lround (geo.camera().b) };
        Vec anchor = centre;
        int index = tonicFifthsIndex (pitchClass (centre), false);
        if (model.key.known())
        {
            anchor = nearestNode (model.key.tonic, centre);
            index = tonicFifthsIndex (model.key.tonic, model.key.minor);
        }
        return LatticeModel::pretty (noteNameOfFifths (literalFifthsIndex (node, anchor, index)).c_str());
    }

    void LatticeView::recentre()
    {
        auto& v = processor.editorView();
        v.zoom = 1.0;
        if (model.source.present || model.generated.present)
            followChords (frozen);
        else
        {
            targetA = 0.75;
            targetB = 0.5;
            easing = ! frozen;
            if (frozen)
            {
                v.centreA = targetA;
                v.centreB = targetB;
            }
        }
        updateGeometry();
        repaint();
    }

    void LatticeView::zoomBy (double factor, juce::Point<float> about)
    {
        auto& v = processor.editorView();
        double a0, b0, a1, b1;
        geo.toLattice (about.x, about.y, a0, b0);
        v.zoom = juce::jlimit (0.45, 2.6, v.zoom * factor);
        updateGeometry();
        geo.toLattice (about.x, about.y, a1, b1);
        v.centreA += a0 - a1;
        v.centreB += b0 - b1;
        targetA = v.centreA;
        targetB = v.centreB;
        easing = false;
        updateGeometry();
        repaint();
    }

    void LatticeView::panBy (juce::Point<float> d)
    {
        auto& v = processor.editorView();
        const double s = geo.spacing();
        const double db = (d.y / s) / LatticeGeometry::rowHeight;
        const double dx = -d.x / s;
        v.centreB += db;
        v.centreA += dx - db * 0.5;
        targetA = v.centreA;
        targetB = v.centreB;
        easing = false;
        updateGeometry();
        repaint();
    }

    // Keeps the source and generated chords in view: eases to the middle of both when either
    // leaves the view's inner part.
    void LatticeView::followChords (bool snap)
    {
        if (getWidth() <= 0)
            return;
        std::vector<Vec> nodes;
        for (const auto* c : { &model.source, &model.generated })
            if (c->present)
                for (int i = 0; i < c->shape.size; ++i)
                    nodes.push_back (c->shape.at[(size_t) i]);
        if (nodes.empty())
            return;
        const double inset = geo.spacing() * 0.7;
        bool inside = true;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const auto& n : nodes)
        {
            const auto p = geo.toScreen (n);
            inside = inside && p.x > inset && p.x < getWidth() - inset && p.y > inset + 20 && p.y < getHeight() - inset - 40;
            minX = std::min (minX, p.x); maxX = std::max (maxX, p.x);
            minY = std::min (minY, p.y); maxY = std::max (maxY, p.y);
        }
        if (inside && ! snap)
            return;
        double a, b;
        geo.toLattice ((minX + maxX) * 0.5, (minY + maxY) * 0.5 + 10.0, a, b);
        targetA = a;
        targetB = b;
        if (snap || frozen)
        {
            processor.editorView().centreA = a;
            processor.editorView().centreB = b;
            easing = false;
            updateGeometry();
        }
        else
            easing = true;
    }

    void LatticeView::modelChanged()
    {
        if (processor.editorView().follow)
            followChords (false);
        hoverGhost = hovering ? ghostAt (hoverAt) : -1;
        repaint();
    }

    bool LatticeView::tick (double seconds)
    {
        if (frozen)
            return false;
        clock += seconds;
        bool again = false;
        if (easing)
        {
            auto& v = processor.editorView();
            const double k = juce::jmin (1.0, seconds * 7.0);
            v.centreA += (targetA - v.centreA) * k;
            v.centreB += (targetB - v.centreB) * k;
            if (std::abs (targetA - v.centreA) < 0.002 && std::abs (targetB - v.centreB) < 0.002)
            {
                v.centreA = targetA;
                v.centreB = targetB;
                easing = false;
            }
            updateGeometry();
            again = true;
        }
        // Sounding nodes pulse.
        for (int n = 0; n < 128 && ! again; ++n)
            again = model.sourceVelocity[(size_t) n] != 0 || model.generatedVelocity[(size_t) n] != 0;
        if (again)
            repaint();
        return again;
    }

    void LatticeView::freezeAnimation (bool f)
    {
        frozen = f;
        clock = 0.0;
        if (f && easing)
        {
            processor.editorView().centreA = targetA;
            processor.editorView().centreB = targetB;
            easing = false;
            updateGeometry();
        }
    }

    void LatticeView::forget()
    {
        held.reset();
        builtSet = false;
        repaint();
    }

    //==========================================================================
    // Direct play.

    std::vector<int> LatticeView::heldPitches() const
    {
        std::vector<int> v;
        for (int n = 0; n < 128; ++n)
            if (notes.holds (n, EditorNotes::lattice))
                v.push_back (n);
        return v;
    }

    // Pitches for `mask` near what the lattice holds: common tones stay where they are, new
    // tones go to the pitch nearest the old chord's middle (a glide moves one voice by a step).
    // From nothing, the root goes in G3..F♯4 with the rest stacked above it.
    std::vector<int> LatticeView::voiceNear (PcMask mask) const
    {
        std::vector<int> out;
        const auto old = heldPitches();
        if (old.empty())
        {
            const int root = chordRoot (mask, -1);
            const int r = 55 + mod12 (root - 7);
            for (int i = 0; i < 12; ++i)
                if (hasPc (mask, root + i))
                    out.push_back (r + i);
            return out;
        }
        double centre = 0.0;
        for (int p : old)
            centre += p;
        centre /= (double) old.size();
        std::vector<bool> placed (12, false);
        for (int p : old)
            if (hasPc (mask, p) && ! placed[(size_t) mod12 (p)])
            {
                out.push_back (p);
                placed[(size_t) mod12 (p)] = true;
            }
        for (int pc = 0; pc < 12; ++pc)
            if (hasPc (mask, pc) && ! placed[(size_t) pc])
            {
                int best = 0;
                double bestD = 1e9;
                for (int p = pc; p < 128; p += 12)
                    if (std::abs (p - centre) < bestD)
                    {
                        bestD = std::abs (p - centre);
                        best = p;
                    }
                out.push_back (best);
            }
        std::sort (out.begin(), out.end());
        return out;
    }

    void LatticeView::setHeldPitches (const std::vector<int>& pitches)
    {
        for (int n : heldPitches())
            if (std::find (pitches.begin(), pitches.end(), n) == pitches.end())
                notes.release (n, EditorNotes::lattice);
        for (int n : pitches)
            notes.press (n, 100, EditorNotes::lattice);
    }

    void LatticeView::pressTriangle (const Triangle& t)
    {
        if (processor.editorView().sticky && held && *held == t && ! builtSet)
        {
            releaseLattice();                    // clicked again: let go
            return;
        }
        if (builtSet)
        {
            notes.releaseHolder (EditorNotes::lattice);
            builtSet = false;
        }
        setHeldPitches (voiceNear (t.mask()));
        held = t;
        repaint();
    }

    void LatticeView::glideTo (const Triangle& t)
    {
        if (held && *held == t)
            return;
        setHeldPitches (voiceNear (t.mask()));
        held = t;
        repaint();
    }

    void LatticeView::toggleNode (Vec node)
    {
        const int pc = pitchClass (node);
        if (held && ! builtSet)
        {
            // A shift-click after a triangle builds on it.
            held.reset();
        }
        builtSet = true;
        for (int n : heldPitches())
            if (mod12 (n) == pc)
            {
                notes.release (n, EditorNotes::lattice);
                repaint();
                return;
            }
        auto old = heldPitches();
        const PcMask mask = (PcMask) [&] { unsigned m = pcBit (pc); for (int n : old) m |= pcBit (n); return m; }();
        setHeldPitches (voiceNear (mask));
        repaint();
    }

    void LatticeView::releaseLattice()
    {
        notes.releaseHolder (EditorNotes::lattice);
        held.reset();
        builtSet = false;
        repaint();
    }

    bool LatticeView::ghostsClickable() const
    {
        return model.generated.present && (model.mode == (int) Mode::Walk || model.mode == (int) Mode::Explore);
    }

    juce::Point<float> LatticeView::ghostChipCentre (int i) const
    {
        const auto& g = model.ghosts[(size_t) i];
        return screen (g.a, g.b);
    }

    int LatticeView::ghostAt (juce::Point<float> p) const
    {
        if (! model.generated.present)
            return -1;
        const float r = (float) chipRadius() + 2.0f;
        int best = -1;
        float bestD = r;
        for (int i = 0; i < (int) model.ghosts.size(); ++i)
        {
            const float d = ghostChipCentre (i).getDistanceFrom (p);
            if (d <= bestD)
            {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    bool LatticeView::clickGhost (int i)
    {
        if (i < 0 || i >= (int) model.ghosts.size() || ! ghostsClickable() || ! model.ghosts[(size_t) i].available)
            return false;
        return processor.sendEditorCommand (EditorCommand::operatorKey (model.ghosts[(size_t) i].key));
    }

    //==========================================================================
    // The mouse.

    void LatticeView::mouseDown (const juce::MouseEvent& e)
    {
        if (onFocusWanted)
            onFocusWanted();
        lastDrag = e.position;
        releaseOnUp = false;
        if (e.mods.isPopupMenu() || e.mods.isCommandDown() || e.mods.isMiddleButtonDown())
        {
            drag = Drag::pan;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }
        if (e.getNumberOfClicks() > 1)
            return;                                  // the double-click recentres
        if (const int gi = ghostAt (e.position); gi >= 0 && ghostsClickable())
        {
            clickGhost (gi);
            drag = Drag::none;
            return;
        }
        if (e.mods.isShiftDown())
        {
            double d = 0.0;
            const Vec n = geo.nearestNode (e.position.x, e.position.y, d);
            if (d <= nodeRadius() * 1.4)
            {
                toggleNode (n);
                drag = Drag::none;
                return;
            }
        }
        const Triangle t = geo.triangleAt (e.position.x, e.position.y);
        const bool wasHeld = held && *held == t;
        pressTriangle (t);
        drag = Drag::glide;
        releaseOnUp = ! processor.editorView().sticky && ! (wasHeld && ! held);
    }

    void LatticeView::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Drag::pan)
        {
            panBy (e.position - lastDrag);
            lastDrag = e.position;
            return;
        }
        if (drag == Drag::glide && held)
            glideTo (geo.triangleAt (e.position.x, e.position.y));
    }

    void LatticeView::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::pan)
            setMouseCursor (juce::MouseCursor::NormalCursor);
        if (drag == Drag::glide && releaseOnUp)
            releaseLattice();
        drag = Drag::none;
    }

    void LatticeView::mouseMove (const juce::MouseEvent& e)
    {
        hovering = true;
        hoverAt = e.position;
        const int gi = ghostAt (e.position);
        juce::String t;
        if (gi >= 0)
            t = model.ghosts[(size_t) gi].description;
        else
        {
            const Triangle tri = geo.triangleAt (e.position.x, e.position.y);
            t = model.longChordName (tri.mask());
        }
        setMouseCursor (gi >= 0 && ghostsClickable() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (gi != hoverGhost || t != tip)
        {
            hoverGhost = gi;
            tip = t;
            repaint();
        }
        else
            repaint (juce::Rectangle<int> ((int) e.position.x - 300, (int) e.position.y - 60, 600, 120));
    }

    void LatticeView::mouseExit (const juce::MouseEvent&)
    {
        hovering = false;
        hoverGhost = -1;
        tip = {};
        repaint();
    }

    void LatticeView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (! e.mods.isShiftDown())
            recentre();
    }

    void LatticeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // A trackpad's two fingers pan; a wheel (or ⌘ + trackpad) zooms.
        if (w.isSmooth && ! e.mods.isCommandDown())
            panBy ({ w.deltaX * 160.0f * (w.isReversed ? -1.0f : 1.0f), w.deltaY * 160.0f * (w.isReversed ? -1.0f : 1.0f) });
        else
            zoomBy (std::pow (1.15, w.deltaY * 4.0), e.position);
    }

    void LatticeView::mouseMagnify (const juce::MouseEvent& e, float scale)
    {
        zoomBy (scale, e.position);
    }

    //==========================================================================
    // Drawing.

    void LatticeView::renderGrid()
    {
        const auto& k = ink();
        const auto& c = geo.camera();
        juce::String key;
        key << getWidth() << "x" << getHeight() << ":" << c.a << ":" << c.b << ":" << c.spacing << ":" << (int) k.dark << ":"
            << model.key.tonic << (int) model.key.minor;
        if (key == gridKey && grid.isValid())
            return;
        gridKey = key;
        const float scale = 1.0f;
        grid = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics g (grid);
        g.addTransform (juce::AffineTransform::scale (scale));

        // The page: paper with a soft darker edge.
        g.fillAll (k.ground);
        juce::ColourGradient vignette (k.ground.withAlpha (0.0f), getWidth() * 0.5f, getHeight() * 0.45f,
                                       k.groundEdge, 0.0f, 0.0f, true);
        g.setGradientFill (vignette);
        g.fillRect (getLocalBounds());

        int aLo, aHi, bLo, bHi;
        geo.visible (aLo, aHi, bLo, bHi, 1.5);

        // The key: every triad inside it tinted (diagonal bands across the plane), its compact
        // copy nearest the camera stronger, the tonic ringed.
        const PcMask km = model.key.known() ? keyMask (model.key.tonic, model.key.minor) : 0;
        if (km != 0)
        {
            for (int b = bLo; b <= bHi; ++b)
                for (int a = aLo; a <= aHi; ++a)
                    for (const bool major : { true, false })
                    {
                        const Triangle t { { a, b }, major };
                        if ((t.mask() & ~km) != 0)
                            continue;
                        const auto n = t.nodes();
                        juce::Path p;
                        p.addTriangle (screen (n[0]), screen (n[1]), screen (n[2]));
                        g.setColour (k.band);
                        g.fillPath (p);
                    }
            const Vec centre { (int) std::lround (c.a), (int) std::lround (c.b) };
            const Vec tonic = nearestNode (model.key.tonic, centre);
            const Vec major = model.key.minor ? tonic + relativeMajor : tonic;
            for (int b = 0; b <= 1; ++b)
                for (int a = -1; a <= 2; ++a)
                    for (const bool up : { true, false })
                    {
                        const Triangle t { major + Vec { a, b }, up };
                        const auto n = t.nodes();
                        bool inBand = true;
                        for (const auto& v : n)
                            inBand = inBand && std::find (bandOffsets.begin(), bandOffsets.end(), v - major) != bandOffsets.end();
                        if (! inBand)
                            continue;
                        juce::Path p;
                        p.addTriangle (screen (n[0]), screen (n[1]), screen (n[2]));
                        g.setColour (k.band.withMultipliedAlpha (1.4f));
                        g.fillPath (p);
                    }
        }

        // The lines: fifths, major thirds, minor thirds.
        const float lw = (float) juce::jlimit (0.8, 1.6, geo.spacing() / 60.0);
        g.setColour (k.line.withAlpha (k.dark ? 0.42f : 0.55f));
        for (int b = bLo; b <= bHi; ++b)
            for (int a = aLo; a <= aHi; ++a)
            {
                const auto p = screen (Vec { a, b });
                for (const Vec d : { fifthStep, majorThirdStep, minorThirdStep })
                {
                    const auto q = screen (Vec { a, b } + d);
                    g.drawLine ({ p, q }, lw);
                }
            }

        // The nodes, spelled.
        const float r = (float) nodeRadius();
        const auto noteFont = Ink::note (r * 0.98f);
        g.setFont (noteFont);
        const Vec tonicNode = model.key.known() ? nearestNode (model.key.tonic, Vec { (int) std::lround (c.a), (int) std::lround (c.b) }) : Vec { 9999, 9999 };
        for (int b = bLo; b <= bHi; ++b)
            for (int a = aLo; a <= aHi; ++a)
            {
                const Vec v { a, b };
                const auto p = screen (v);
                if (! geo.onScreen ({ p.x, p.y }, r * 2))
                    continue;
                const bool inKey = km != 0 && hasPc (km, pitchClass (v));
                const juce::Rectangle<float> disc (p.x - r, p.y - r, 2 * r, 2 * r);
                g.setColour (k.node);
                g.fillEllipse (disc);
                if (inKey)
                {
                    g.setColour (k.band.withMultipliedAlpha (1.6f));
                    g.fillEllipse (disc);
                }
                g.setColour (k.line.withAlpha (inKey ? 0.85f : 0.6f));
                g.drawEllipse (disc, lw);
                if (v == tonicNode)
                {
                    g.setColour (k.bandStrong);
                    g.drawEllipse (disc.expanded (3.5f), 2.0f);
                }
                g.setColour (inKey ? k.text : k.text.withAlpha (0.78f));
                g.drawText (nodeLabel (v), disc.expanded (6.0f, 0.0f).translated (0.0f, -0.5f), juce::Justification::centred, false);
            }
    }

    void LatticeView::drawShape (juce::Graphics& g, const Embedding& e, juce::Colour colour, float fillAlpha, bool copies) const
    {
        if (e.size == 0)
            return;
        const auto tris = trianglesOf (e);
        const auto info = classify (e);
        for (int ci = -3; ci <= 3; ++ci)
            for (int cj = -3; cj <= 3; ++cj)
            {
                const bool primary = ci == 0 && cj == 0;
                if (! primary && ! copies)
                    continue;
                const Vec off = copyOffset (ci, cj);
                const float alpha = primary ? 1.0f : 0.28f;
                bool visible = false;
                for (int i = 0; i < e.size && ! visible; ++i)
                {
                    const auto p = geo.toScreen (e.at[(size_t) i] + off);
                    visible = geo.onScreen (p, geo.spacing());
                }
                if (! visible)
                    continue;
                // Filled triangles, then the edges between neighbours, then a loose outline.
                for (int t = 0; t < tris.size; ++t)
                {
                    const auto& idx = tris.tones[(size_t) t];
                    juce::Path p;
                    p.addTriangle (screen (e.at[(size_t) idx[0]] + off), screen (e.at[(size_t) idx[1]] + off), screen (e.at[(size_t) idx[2]] + off));
                    g.setColour (colour.withAlpha (fillAlpha * alpha));
                    g.fillPath (p);
                }
                g.setColour (colour.withAlpha (0.9f * alpha));
                const float w = (float) juce::jlimit (2.0, 4.5, geo.spacing() / 22.0);
                for (int i = 0; i < e.size; ++i)
                    for (int j = i + 1; j < e.size; ++j)
                        if (adjacent (e.at[(size_t) i], e.at[(size_t) j]))
                            g.drawLine ({ screen (e.at[(size_t) i] + off), screen (e.at[(size_t) j] + off) }, w);
                if (info.shape == Shape::Other && e.size > 1)
                {
                    // A looser shape: a dashed outline round its nodes (their convex hull).
                    std::vector<juce::Point<float>> pts;
                    for (int i = 0; i < e.size; ++i)
                        pts.push_back (screen (e.at[(size_t) i] + off));
                    std::sort (pts.begin(), pts.end(), [] (auto a, auto b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
                    std::vector<juce::Point<float>> hull;
                    auto cross = [] (auto o, auto a, auto b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
                    for (int pass = 0; pass < 2; ++pass)
                    {
                        const size_t start = hull.size();
                        for (const auto& p : pts)
                        {
                            while (hull.size() >= start + 2 && cross (hull[hull.size() - 2], hull.back(), p) <= 0)
                                hull.pop_back();
                            hull.push_back (p);
                        }
                        hull.pop_back();
                        std::reverse (pts.begin(), pts.end());
                    }
                    juce::Path outline;
                    for (size_t i = 0; i < hull.size(); ++i)
                        i == 0 ? outline.startNewSubPath (hull[i]) : outline.lineTo (hull[i]);
                    outline.closeSubPath();
                    juce::Path dashed;
                    const float dashes[] = { 5.0f, 4.0f };
                    juce::PathStrokeType (1.6f).createDashedStroke (dashed, outline, dashes, 2);
                    g.setColour (colour.withAlpha (0.75f * alpha));
                    g.fillPath (dashed);
                    if (hull.size() >= 3)
                    {
                        g.setColour (colour.withAlpha (fillAlpha * 0.4f * alpha));
                        g.fillPath (outline);
                    }
                }
            }
    }

    void LatticeView::drawSounding (juce::Graphics& g)
    {
        const auto& k = ink();
        const float r = (float) nodeRadius();
        const auto& src = model.source;
        const auto& gen = model.generated;
        auto velocityOf = [] (const std::array<uint8_t, 128>& v, int pc)
        {
            int best = 0;
            for (int n = pc; n < 128; n += 12)
                best = juce::jmax (best, (int) v[(size_t) n]);
            return best;
        };
        const float pulse = frozen ? 0.5f : 0.5f + 0.5f * (float) std::sin (clock * juce::MathConstants<double>::twoPi * 1.1);

        // Every node of each chord's primary copy: filled with its ink; a common tone (in both
        // chords) is vermilion ringed in teal. Other copies of a sounding pitch class: a ring.
        struct Mark { Vec at; bool s, gn; int velocity; };
        std::vector<Mark> marks;
        auto addMark = [&] (Vec v, bool s, bool gn, int vel)
        {
            for (auto& m : marks)
                if (m.at == v) { m.s = m.s || s; m.gn = m.gn || gn; m.velocity = juce::jmax (m.velocity, vel); return; }
            marks.push_back ({ v, s, gn, vel });
        };
        if (src.present)
            for (int i = 0; i < src.shape.size; ++i)
            {
                const int pc = src.shape.pc[(size_t) i];
                addMark (src.shape.at[(size_t) i], true, gen.present && gen.shape.contains (pc) && gen.shape.nodeOf (pc) == src.shape.at[(size_t) i],
                         velocityOf (model.sourceVelocity, pc));
            }
        if (gen.present)
            for (int i = 0; i < gen.shape.size; ++i)
            {
                const int pc = gen.shape.pc[(size_t) i];
                addMark (gen.shape.at[(size_t) i], src.present && src.shape.contains (pc) && src.shape.nodeOf (pc) == gen.shape.at[(size_t) i], true,
                         juce::jmax (velocityOf (model.generatedVelocity, pc), velocityOf (model.sourceVelocity, pc)));
            }

        // Faint rings on the other copies.
        int aLo, aHi, bLo, bHi;
        geo.visible (aLo, aHi, bLo, bHi, 1.0);
        for (int b = bLo; b <= bHi; ++b)
            for (int a = aLo; a <= aHi; ++a)
            {
                const Vec v { a, b };
                const int pc = pitchClass (v);
                const bool s = src.present && hasPc (src.chord.mask, pc), gn = gen.present && hasPc (gen.chord.mask, pc);
                if (! s && ! gn)
                    continue;
                bool primary = false;
                for (const auto& m : marks)
                    primary = primary || m.at == v;
                if (primary)
                    continue;
                const auto p = screen (v);
                g.setColour ((gn ? k.generated : k.source).withAlpha (0.45f));
                g.drawEllipse (p.x - r - 2.0f, p.y - r - 2.0f, 2 * r + 4.0f, 2 * r + 4.0f, 1.4f);
            }

        g.setFont (Ink::note (r * 0.98f, true));
        for (const auto& m : marks)
        {
            const auto p = screen (m.at);
            const float grow = 1.0f + 0.16f * (m.velocity / 127.0f) * pulse;
            const float rr = r * grow;
            const juce::Rectangle<float> disc (p.x - rr, p.y - rr, 2 * rr, 2 * rr);
            const bool sounding = m.velocity > 0;
            g.setColour ((m.gn ? k.generated : k.source).withAlpha (sounding ? 1.0f : 0.55f));
            g.fillEllipse (disc);
            if (m.s && m.gn)
            {
                g.setColour (k.source);
                g.drawEllipse (disc.expanded (3.0f), 3.0f);
            }
            g.setColour (k.onInk);
            g.drawText (nodeLabel (m.at), disc.expanded (6.0f, 0.0f).translated (0.0f, -0.5f), juce::Justification::centred, false);
        }
    }

    void LatticeView::drawTrail (juce::Graphics& g)
    {
        const auto& k = ink();
        const auto& t = model.trail;
        if (t.size() < 2)
            return;
        const float w = (float) juce::jlimit (2.0, 5.0, geo.spacing() / 18.0);
        for (size_t i = 1; i < t.size(); ++i)
        {
            const float age = (float) (t.size() - i) / (float) t.size();         // 0 newest .. 1 oldest
            g.setColour (k.generated.withAlpha (0.55f * (1.0f - age) + 0.06f));
            juce::Path p;
            p.startNewSubPath (screen (t[i - 1].a, t[i - 1].b));
            p.lineTo (screen (t[i].a, t[i].b));
            g.strokePath (p, juce::PathStrokeType (w * (1.0f - 0.5f * age), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (juce::Rectangle<float> (w * 1.6f, w * 1.6f).withCentre (screen (t[i - 1].a, t[i - 1].b)));
        }
    }

    void LatticeView::drawPath (juce::Graphics& g)
    {
        const auto& k = ink();
        if (! model.source.present || model.path.empty())
            return;
        // W op by op: an arrow from each chord's centre to the next, labelled with its op.
        juce::Point<float> from = screen (model.source.a, model.source.b);
        g.setFont (Ink::ui (11.0f, true));
        const int n = (int) model.path.size();
        for (int i = 0; i < n; ++i)
        {
            const auto to = screen (model.path[(size_t) i].a, model.path[(size_t) i].b);
            if (from.getDistanceFrom (to) > 2.0f)
            {
                juce::Line<float> l (from, to);
                const float trim = juce::jmin (6.0f, l.getLength() * 0.2f);
                l = juce::Line<float> (l.getPointAlongLine (trim), l.getPointAlongLine (l.getLength() - trim));
                juce::Path arrow;
                arrow.addArrow (l, 1.4f, 9.0f, 8.0f);
                g.setColour (k.text.withAlpha (0.75f));
                g.fillPath (arrow);
                const auto mid = l.getPointAlongLineProportionally (0.5f) + juce::Point<float> (0.0f, -9.0f);
                g.setColour (k.text);
                g.drawText (LatticeModel::pretty (printWord ([&] { Word w; w.push (model.word[i]); return w; }()).c_str()),
                            juce::Rectangle<float> (40.0f, 14.0f).withCentre (mid), juce::Justification::centred, false);
            }
            from = to;
        }
    }

    void LatticeView::drawCycle (juce::Graphics& g)
    {
        const auto& k = ink();
        const auto& c = model.cycle;
        if (c.size() < 2)
            return;
        juce::Path p;
        p.startNewSubPath (screen (c[0].a, c[0].b));
        for (size_t i = 1; i < c.size(); ++i)
            p.lineTo (screen (c[i].a, c[i].b));
        juce::Path dashed;
        const float dashes[] = { 2.0f, 5.0f };
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (k.ghost.withAlpha (0.8f));
        g.fillPath (dashed);
        for (size_t i = 0; i < c.size(); ++i)
        {
            const auto q = screen (c[i].a, c[i].b);
            const bool at = (int) i == model.cycleAt;
            const float d = at ? 9.0f : 5.0f;
            g.setColour (at ? k.generated : k.ghost.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (q));
        }
        // The loop closes on the torus: the last point is the start again.
        g.setColour (k.ghost);
        g.setFont (Ink::ui (13.0f, true));
        g.drawText (utf8 ("\xe2\x86\xba"), juce::Rectangle<float> (20.0f, 16.0f).withCentre (screen (c.back().a, c.back().b) + juce::Point<float> (12.0f, -12.0f)),
                    juce::Justification::centred, false);
    }

    void LatticeView::drawGhosts (juce::Graphics& g)
    {
        const auto& k = ink();
        if (! model.generated.present)
            return;
        const bool clickable = ghostsClickable();
        const float cr = (float) chipRadius();
        // The hovered ghost's shape, outlined.
        if (hoverGhost >= 0 && model.ghosts[(size_t) hoverGhost].available)
            drawShape (g, model.ghosts[(size_t) hoverGhost].shape, k.ghost, 0.10f, false);

        for (int i = 0; i < (int) model.ghosts.size(); ++i)
        {
            const auto& gh = model.ghosts[(size_t) i];
            const auto c = ghostChipCentre (i);
            const auto chip = juce::Rectangle<float> (2 * cr, 2 * cr).withCentre (c);
            float alpha = clickable ? 1.0f : 0.55f;
            if (! gh.available)
                alpha *= 0.35f;
            if (gh.faint)
                alpha *= 0.35f;
            const bool solid = gh.available && model.inKey && model.key.known() && gh.inKey;
            const bool hot = i == hoverGhost;
            g.setColour ((solid ? k.ghost : k.ground).withAlpha (alpha));
            g.fillEllipse (chip);
            g.setColour ((hot ? k.generated : k.ghost).withAlpha (alpha));
            g.drawEllipse (chip, hot ? 2.2f : 1.4f);
            g.setColour ((solid ? k.onInk : (hot ? k.generated : k.text)).withAlpha (alpha));
            g.setFont (Ink::ui (gh.label.length() > 2 ? cr * 0.78f : cr * 1.02f, true));
            g.drawText (gh.label, chip.expanded (4.0f, 0.0f), juce::Justification::centred, false);
            if (! gh.available)
            {
                g.setColour (k.ghost.withAlpha (alpha));
                g.drawLine ({ chip.getBottomLeft(), chip.getTopRight() }, 1.0f);
            }
        }
    }

    void LatticeView::drawTip (juce::Graphics& g)
    {
        const auto& k = ink();
        if (! hovering || tip.isEmpty())
            return;
        const auto f = Ink::ui (12.5f, hoverGhost >= 0);
        const float w = juce::GlyphArrangement::getStringWidth (f, tip) + 14.0f;
        auto r = juce::Rectangle<float> (w, 20.0f).withPosition (hoverAt + juce::Point<float> (14.0f, 12.0f));
        if (r.getRight() > getWidth() - 4) r.setX (hoverAt.x - w - 10.0f);
        if (r.getBottom() > getHeight() - 24) r.setY (hoverAt.y - 32.0f);
        g.setColour (k.wash);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (hoverGhost >= 0 ? k.text : k.textFaint);
        g.setFont (f);
        g.drawText (tip, r, juce::Justification::centred, false);
    }

    void LatticeView::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        renderGrid();
        g.drawImageAt (grid, 0, 0);

        drawCycle (g);
        drawTrail (g);
        if (model.source.present)
            drawShape (g, model.source.shape, k.source, 0.22f, true);
        if (model.generated.present)
            drawShape (g, model.generated.shape, k.generated, 0.26f, true);
        drawPath (g);
        drawGhosts (g);
        drawSounding (g);

        // The foot of the page fades to paper, so the word bar and the hints read clearly.
        {
            const float top = (float) getHeight() - 92.0f;
            juce::ColourGradient fade (k.ground.withAlpha (0.0f), 0.0f, top, k.ground.withAlpha (0.93f), 0.0f, top + 52.0f, false);
            g.setGradientFill (fade);
            g.fillRect (0.0f, top, (float) getWidth(), 92.0f);
        }

        // The bottom line: how to play it, Follow, Release.
        follow.setText (utf8 (processor.editorView().follow ? "\xe2\x97\x8f follow" : "\xe2\x97\x8b follow"));
        release.setEnabled (notes.any (EditorNotes::lattice));
        g.setFont (Ink::ui (11.0f));
        g.setColour (k.textFaint);
        g.drawText (utf8 ("click a triangle \xc2\xb7 drag to glide \xc2\xb7 shift-click notes \xc2\xb7 right-drag pans \xc2\xb7 scroll zooms \xc2\xb7 double-click recentres"),
                    juce::Rectangle<int> (12, getHeight() - 20, getWidth() - 160, 18), juce::Justification::centredLeft, true);
        drawTip (g);
    }
}
