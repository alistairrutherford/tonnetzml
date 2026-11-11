// The lattice (PLAN.md §5, "Lattice view"): an endless plane of nodes spelled in the key, with
// the key's band tinted, the source chord in teal and W(chord) in vermilion as filled shapes,
// common tones ringed in both, W's path between them, the trail of the last chords, Orbit's
// cycle, and the ghost neighbours one op away as chips that name and (in Walk and Explore)
// append their op.
//
// Playing it (direct play): click a triangle to play its triad, drag across triangles to glide
// (common tones held, one voice moving), shift-click nodes to build any set. With Sticky on (the
// default) what was clicked stays down until it is clicked again, another triangle replaces it,
// or Release; with it off it sounds while the button is down. The camera: right-, ⌘- or
// middle-drag (or a trackpad's two fingers) pans, the wheel or a pinch zooms, double-click
// recentres; with Follow on it eases to keep the chords in view.
//
// Drawing: the grid (lines, nodes, labels, key band) is cached in an image and redrawn only
// when the camera, size, theme or key changes; the chords, ghosts and trail are drawn over it.
#pragma once

#include "EditorNotes.h"
#include "Hud.h"
#include "Ink.h"
#include "LatticeGeometry.h"
#include "LatticeModel.h"

namespace tonnetz::ui
{
    class LatticeView final : public juce::Component
    {
    public:
        LatticeView (TonnetzProcessor&, const LatticeModel&, EditorNotes&);

        // The model changed: follow the chords, repaint.
        void modelChanged();
        // Animation (camera easing, pulses): true if it repainted.
        bool tick (double seconds);
        // Tests: no easing, no pulse, a fixed clock.
        void freezeAnimation (bool frozen);
        // The engine forgot every key (stop, Enable off): so does the lattice.
        void forget();

        // Geometry and labels.
        const LatticeGeometry& geometry() const noexcept { return geo; }
        juce::String nodeLabel (Vec node) const;
        void recentre();
        void zoomBy (double factor, juce::Point<float> about);
        void panBy (juce::Point<float> pixels);

        // Direct play (what the mouse does; public for tests).
        void pressTriangle (const Triangle&);
        void glideTo (const Triangle&);
        void toggleNode (Vec node);
        void releaseLattice();
        // Holds these exact pitches as the lattice's set (the capture strip's recall, Phase 5).
        void holdSet (const std::vector<int>& pitches);
        int ghostAt (juce::Point<float>) const;          // index into the model's ghosts, or -1
        bool clickGhost (int index);                     // false if it can't be clicked here
        juce::Point<float> ghostChipCentre (int index) const;
        bool ghostsClickable() const;
        std::optional<Triangle> heldTriangle() const { return held; }

        // Free ops on the generated chord (Phase 6): click one of its nodes to Pivot on it, the
        // middle of one of its edges to Flip through it, a node across an edge (marked +) to
        // Grow across it. Appended to W where the word bar can edit it.
        struct LatticeOp
        {
            enum Kind : uint8_t { pivot, flip, grow } kind = pivot;
            Op op;
            double a = 0.0, b = 0.0;          // where it is clicked (lattice coordinates)
            Vec u, v;                         // the node (pivot, grow) / the edge's ends (flip, grow)
            PcMask result = 0;
            juce::String description;         // "Pivot on E → A minor"
        };
        const std::vector<LatticeOp>& latticeOps() const noexcept { return lops; }
        int latticeOpAt (juce::Point<float>) const;
        bool clickLatticeOp (int index);       // false if it can't be clicked here
        juce::Point<float> latticeOpCentre (int index) const { return screen (lops[(size_t) index].a, lops[(size_t) index].b); }
        std::function<bool()> canEditWord;     // the word bar's editable()
        std::function<void (const Op&)> appendOp;

        // Hover audition (Phase 6): what is heard through Preview tone (pitches), for tests.
        const std::vector<int>& auditioning() const noexcept { return auditioned; }
        void hoverAt_ (juce::Point<float> p);  // what moving the mouse there does
        // A restored state replaced the view: take its camera.
        void viewRestored();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scale) override;

        std::function<void()> onFocusWanted;

    private:
        const Ink& ink() const { return Ink::of (processor.editorView().dark); }
        void updateGeometry();
        void followChords (bool snap);
        void setHeldPitches (const std::vector<int>& pitches);
        std::vector<int> heldPitches() const;
        std::vector<int> voiceNear (PcMask mask) const;
        double nodeRadius() const;
        double chipRadius() const;

        void renderGrid();
        void drawShape (juce::Graphics&, const Embedding&, juce::Colour, float fillAlpha, bool copies) const;
        void drawSounding (juce::Graphics&);
        void drawGhosts (juce::Graphics&);
        void drawTrail (juce::Graphics&);
        void drawPath (juce::Graphics&);
        void drawCycle (juce::Graphics&);
        void drawTip (juce::Graphics&);
        void drawLatticeOps (juce::Graphics&);
        void rebuildLatticeOps();
        bool opsEditable() const;
        void audition (PcMask mask);
        std::vector<int> voiceLikeGenerated (PcMask mask) const;
        juce::Point<float> screen (double a, double b) const { const auto p = geo.toScreen (a, b); return { (float) p.x, (float) p.y }; }
        juce::Point<float> screen (Vec v) const { return screen ((double) v.a, (double) v.b); }

        TonnetzProcessor& processor;
        const LatticeModel& model;
        EditorNotes& notes;
        LatticeGeometry geo;
        HudButton follow, release;

        // Camera easing.
        double targetA = 0.75, targetB = 0.5;
        bool easing = false, frozen = false;
        double clock = 0.0;

        // The grid image and what it was drawn for.
        juce::Image grid;
        juce::String gridKey;

        // Direct play.
        enum class Drag { none, glide, pan } drag = Drag::none;
        std::optional<Triangle> held;
        bool builtSet = false;                  // the lattice's notes are a shift-clicked set
        juce::Point<float> lastDrag;
        bool releaseOnUp = false;

        // Hover.
        juce::Point<float> hoverAt;
        bool hovering = false;
        int hoverGhost = -1;
        int hoverOp = -1;
        juce::String tip;

        std::vector<LatticeOp> lops;
        std::vector<int> auditioned;
        PcMask auditionMask = 0;
    };
}
