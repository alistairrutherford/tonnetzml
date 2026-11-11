#include "Hud.h"

namespace tonnetz::ui
{
    namespace
    {
        float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
    }

    //==========================================================================
    HudControl::HudControl (juce::RangedAudioParameter& p, const EditorView& v, juce::String captionText)
        : param (p), view (v), caption (std::move (captionText))
    {
        setComponentID (p.getParameterID());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (false);
    }

    float HudControl::plain() const { return param.convertFrom0to1 (param.getValue()); }

    void HudControl::setPlain (float value)
    {
        const float v = param.getNormalisableRange().snapToLegalValue (value);
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.convertTo0to1 (v));
        param.endChangeGesture();
        refresh();
    }

    void HudControl::step (int delta, bool wrap)
    {
        const auto& r = param.getNormalisableRange();
        const float lo = r.start, hi = r.end;
        float v = plain() + (float) delta;
        if (wrap)
        {
            const float span = hi - lo + 1.0f;
            while (v > hi) v -= span;
            while (v < lo) v += span;
        }
        setPlain (juce::jlimit (lo, hi, v));
    }

    void HudControl::refresh()
    {
        const auto t = valueText();
        if (t != shown)
        {
            shown = t;
            repaint();
        }
    }

    float HudControl::captionWidth() const { return caption.isEmpty() ? 0.0f : textWidth (captionFont(), caption) + 5.0f; }

    int HudControl::preferredWidth() const { return (int) std::ceil (captionWidth() + textWidth (font(), valueText()) + 4.0f); }

    void HudControl::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        auto r = getLocalBounds().toFloat();
        if (caption.isNotEmpty())
        {
            g.setColour (k.textFaint);
            g.setFont (captionFont());
            g.drawText (caption, r.removeFromLeft (captionWidth()), juce::Justification::centredLeft, false);
        }
        g.setColour (isMouseOver() ? k.generated : k.text);
        g.setFont (font());
        g.drawText (valueText(), r, juce::Justification::centredLeft, false);
        if (isMouseOver())
        {
            const float w = textWidth (font(), valueText());
            g.fillRect (r.getX(), r.getCentreY() + 8.0f, w, 1.0f);
        }
    }

    void HudControl::mouseUp (const juce::MouseEvent& e)
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
            showMenu();
        else
            activate (e.mods.isShiftDown());
    }

    void HudControl::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        wheelAccumulator += (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        while (wheelAccumulator > 0.12f) { step (1, false); wheelAccumulator -= 0.12f; }
        while (wheelAccumulator < -0.12f) { step (-1, false); wheelAccumulator += 0.12f; }
    }

    void HudControl::showMenu()
    {
        juce::PopupMenu m;
        const auto& r = param.getNormalisableRange();
        const int lo = (int) r.start, hi = (int) r.end;
        if (hi - lo > 40)
        {
            activate (true);
            return;
        }
        const int current = (int) std::lround (plain());
        for (int v = lo; v <= hi; ++v)
            m.addItem (v - lo + 1, param.getText (param.convertTo0to1 ((float) v), 32), true, v == current);
        juce::Component::SafePointer<HudControl> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, lo] (int chosen)
        {
            if (safe != nullptr && chosen > 0)
                safe->setPlain ((float) (lo + chosen - 1));
        });
    }

    //==========================================================================
    HudToggle::HudToggle (juce::RangedAudioParameter& p, const EditorView& v, juce::String captionText, juce::String onText, juce::String offText)
        : HudControl (p, v, std::move (captionText)), on (std::move (onText)), off (std::move (offText)) {}

    juce::String HudToggle::valueText() const
    {
        const bool isOn = plain() > 0.5f;
        if (off.isNotEmpty())
            return isOn ? on : off;
        return utf8 (isOn ? "\xe2\x97\x8f " : "\xe2\x97\x8b ") + on;
    }

    //==========================================================================
    HudCycle::HudCycle (juce::RangedAudioParameter& p, const EditorView& v, juce::String captionText, juce::String suffixText, int step)
        : HudControl (p, v, std::move (captionText)), suffix (std::move (suffixText)), clickStep (juce::jmax (1, step)) {}

    bool HudCycle::isNumber() const { return dynamic_cast<const juce::AudioParameterInt*> (&param) != nullptr; }

    juce::String HudCycle::valueText() const
    {
        auto t = param.getCurrentValueAsText();
        if (isNumber() && param.getNormalisableRange().start < 0.0f && plain() > 0.0f)
            t = "+" + t;
        return t.replace ("-", utf8 ("\xe2\x88\x92")) + suffix;
    }

    void HudCycle::activate (bool backwards)
    {
        if (! isNumber())
        {
            step (backwards ? -1 : 1, true);
            return;
        }
        // A number moves by its click step and wraps from the top to the bottom.
        const auto& r = param.getNormalisableRange();
        float v = plain() + (float) (backwards ? -clickStep : clickStep);
        if (v > r.end) v = r.start;
        if (v < r.start) v = r.end;
        setPlain (v);
    }

    void HudCycle::showMenu()
    {
        if (! isNumber() || param.getNormalisableRange().end - param.getNormalisableRange().start <= 40)
        {
            HudControl::showMenu();
            return;
        }
        // A long range: a menu of steps.
        juce::PopupMenu m;
        const auto& r = param.getNormalisableRange();
        const int current = (int) std::lround (plain());
        for (int v = (int) r.start; v <= (int) r.end; v += clickStep)
            m.addItem (v + 1000, param.getText (param.convertTo0to1 ((float) v), 32) + suffix, true, v == current);
        juce::Component::SafePointer<HudCycle> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int chosen)
        {
            if (safe != nullptr && chosen > 0)
                safe->setPlain ((float) (chosen - 1000));
        });
    }

    void HudCycle::mouseDown (const juce::MouseEvent&)
    {
        dragStartValue = plain();
        dragged = false;
        if (isNumber())
            param.beginChangeGesture();
    }

    void HudCycle::mouseDrag (const juce::MouseEvent& e)
    {
        if (! isNumber() || e.mods.isPopupMenu())
            return;
        const int dy = -e.getDistanceFromDragStartY();
        if (! dragged && std::abs (dy) < 4)
            return;
        dragged = true;
        const auto& r = param.getNormalisableRange();
        const float v = juce::jlimit (r.start, r.end, dragStartValue + (float) (dy / 4));
        param.setValueNotifyingHost (param.convertTo0to1 (v));
        refresh();
    }

    void HudCycle::mouseUp (const juce::MouseEvent& e)
    {
        if (isNumber())
            param.endChangeGesture();
        if (! dragged)
            HudControl::mouseUp (e);
        dragged = false;
    }

    void HudCycle::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (isNumber())
            setPlain (param.convertFrom0to1 (param.getDefaultValue()));
    }

    //==========================================================================
    HudChoices::HudChoices (juce::RangedAudioParameter& p, const EditorView& v, juce::String captionText, juce::StringArray l, float g)
        : HudControl (p, v, std::move (captionText)), labels (std::move (l)), gap (g) {}

    juce::String HudChoices::valueText() const
    {
        return labels[juce::jlimit (0, labels.size() - 1, (int) std::lround (plain()))];
    }

    juce::Rectangle<float> HudChoices::optionBounds (int index) const
    {
        float x = captionWidth();
        for (int i = 0; i < index; ++i)
            x += textWidth (font(), labels[i]) + gap;
        return { x - gap * 0.5f, 0.0f, textWidth (font(), labels[index]) + gap, (float) getHeight() };
    }

    int HudChoices::optionAt (int x) const
    {
        for (int i = 0; i < labels.size(); ++i)
            if (optionBounds (i).contains ((float) x, 1.0f))
                return i;
        return -1;
    }

    int HudChoices::preferredWidth() const
    {
        float w = captionWidth();
        for (const auto& l : labels)
            w += textWidth (font(), l) + gap;
        return (int) std::ceil (w);
    }

    void HudChoices::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        if (caption.isNotEmpty())
        {
            g.setColour (k.textFaint);
            g.setFont (captionFont());
            g.drawText (caption, juce::Rectangle<float> (0.0f, 0.0f, captionWidth(), (float) getHeight()), juce::Justification::centredLeft, false);
        }
        const int current = juce::jlimit (0, labels.size() - 1, (int) std::lround (plain()));
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto r = optionBounds (i).reduced (gap * 0.5f, 0.0f);
            const bool on = i == current;
            g.setFont (Ink::ui (13.0f, on));
            g.setColour (on ? k.text : (i == hover ? k.generated : k.textFaint));
            g.drawText (labels[i], r.withWidth (r.getWidth() + 8.0f), juce::Justification::centredLeft, false);
            if (on)
            {
                g.setColour (k.generated);
                g.fillRect (r.getX(), r.getCentreY() + 8.0f, textWidth (Ink::ui (13.0f, true), labels[i]), 1.5f);
            }
            if (marked && marked (i) && ! on)
            {
                g.setColour (k.textFaint);
                g.fillEllipse (r.getX() + r.getWidth() * 0.5f - 1.5f, r.getCentreY() + 7.5f, 3.0f, 3.0f);
            }
        }
    }

    void HudChoices::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        const int i = optionAt (e.x);
        if (i >= 0)
            setPlain ((float) i);
    }

    //==========================================================================
    KeyBadge::KeyBadge (juce::RangedAudioParameter& keyMode, juce::RangedAudioParameter& tonic, juce::RangedAudioParameter& minor,
                        juce::RangedAudioParameter& inKey, const EditorView& v, const LatticeModel& m)
        : HudControl (keyMode, v, {}), tonicParam (tonic), minorParam (minor), inKeyParam (inKey), model (m) {}

    juce::String KeyBadge::glyph() const
    {
        const int mode = (int) std::lround (plain());
        if (mode == (int) KeyMode::Off)
            return utf8 ("\xe2\x97\x8b");                           // ○
        if (mode == (int) KeyMode::Hold)
            return utf8 ("\xe2\x97\xbc");                           // ◼
        if (! model.guess.has())
            return utf8 ("\xe2\x97\x8c");                           // ◌
        return utf8 (model.guess.state == GuessState::Confident ? "\xe2\x97\x89" : "\xe2\x97\x8b");   // ◉ ○
    }

    juce::String KeyBadge::valueText() const
    {
        const int mode = (int) std::lround (plain());
        juce::String t;
        if (mode == (int) KeyMode::Off)
            t = "key off";
        else if (mode == (int) KeyMode::Hold)
            t = model.keyText.isNotEmpty() ? model.keyText + " held" : juce::String ("held");
        else
            t = model.guess.has() ? model.keyText : utf8 ("listening\xe2\x80\xa6");
        if (inKeyParam.getValue() > 0.5f)
            t << utf8 (" \xc2\xb7 in key");
        return glyph() + " " + t;
    }

    int KeyBadge::preferredWidth() const { return (int) std::ceil (textWidth (Ink::ui (13.0f, true), valueText()) + 6.0f); }

    void KeyBadge::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        const int mode = (int) std::lround (plain());
        const bool tentative = mode == (int) KeyMode::Listen && (! model.guess.has() || model.guess.state != GuessState::Confident);
        g.setFont (Ink::ui (13.0f, mode != (int) KeyMode::Off && ! tentative));
        g.setColour (isMouseOver() ? k.generated : (mode == (int) KeyMode::Off || tentative ? k.textFaint : k.text));
        g.drawText (valueText(), getLocalBounds().toFloat(), juce::Justification::centredLeft, false);
    }

    void KeyBadge::activate (bool)
    {
        // Listen → Hold → Off → Listen.
        const int mode = (int) std::lround (plain());
        setPlain ((float) (mode == (int) KeyMode::Listen ? (int) KeyMode::Hold : mode == (int) KeyMode::Hold ? (int) KeyMode::Off : (int) KeyMode::Listen));
    }

    void KeyBadge::showMenu()
    {
        juce::PopupMenu m;
        const int mode = (int) std::lround (plain());
        m.addItem (1, "Listen", true, mode == (int) KeyMode::Listen);
        m.addItem (2, "Hold", true, mode == (int) KeyMode::Hold);
        m.addItem (3, "Off", true, mode == (int) KeyMode::Off);
        m.addSeparator();
        juce::PopupMenu major, minor;
        const int tonic = (int) std::lround (tonicParam.convertFrom0to1 (tonicParam.getValue()));
        const bool isMinor = minorParam.getValue() > 0.5f;
        for (int t = 0; t < 12; ++t)
        {
            major.addItem (100 + t, LatticeModel::pretty (noteNameOfFifths (tonicFifthsIndex (t, false)).c_str()) + " major", true, mode == (int) KeyMode::Hold && t == tonic && ! isMinor);
            minor.addItem (200 + t, LatticeModel::pretty (noteNameOfFifths (tonicFifthsIndex (t, true)).c_str()) + " minor", true, mode == (int) KeyMode::Hold && t == tonic && isMinor);
        }
        m.addSubMenu ("Set major key", major);
        m.addSubMenu ("Set minor key", minor);
        m.addSeparator();
        m.addItem (4, "In key (ghosts outside the key faint; Orbit skips them)", true, inKeyParam.getValue() > 0.5f);
        juce::Component::SafePointer<KeyBadge> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int chosen)
        {
            if (safe == nullptr || chosen <= 0)
                return;
            auto set = [] (juce::RangedAudioParameter& p, float v)
            {
                p.beginChangeGesture();
                p.setValueNotifyingHost (p.convertTo0to1 (v));
                p.endChangeGesture();
            };
            if (chosen <= 3)
                safe->setPlain ((float) (chosen == 1 ? 1 : chosen == 2 ? 2 : 0));
            else if (chosen == 4)
                set (safe->inKeyParam, safe->inKeyParam.getValue() > 0.5f ? 0.0f : 1.0f);
            else
            {
                // A key set by hand is held.
                set (safe->tonicParam, (float) (chosen % 100));
                set (safe->minorParam, chosen >= 200 ? 1.0f : 0.0f);
                safe->setPlain ((float) (int) KeyMode::Hold);
            }
            safe->refresh();
        });
    }

    //==========================================================================
    HudButton::HudButton (const EditorView& v, juce::String t, std::function<void()> onClick, float s)
        : click (std::move (onClick)), view (v), text (std::move (t)), size (s)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int HudButton::preferredWidth() const { return (int) std::ceil (textWidth (Ink::ui (size), text) + 6.0f); }

    void HudButton::paint (juce::Graphics& g)
    {
        const auto& k = Ink::of (view.dark);
        g.setFont (Ink::ui (size, lit));
        g.setColour (! isEnabled() ? k.lineFaint : isMouseOver() ? k.generated : (lit ? k.text : k.textFaint));
        g.drawText (text, getLocalBounds().toFloat(), juce::Justification::centred, false);
    }

    //==========================================================================
    namespace
    {
        juce::RangedAudioParameter& paramOf (TonnetzProcessor& p, const char* id)
        {
            auto* param = p.getState().getParameter (id);
            jassert (param != nullptr);
            return *param;
        }
    }

    Header::Header (TonnetzProcessor& p, const LatticeModel& m, std::function<void()> onTheme)
        : processor (p), model (m),
          enable (paramOf (p, params::enable), p.editorView(), {}, utf8 ("\xe2\x97\x8f on"), utf8 ("\xe2\x97\x8b off")),
          mode (paramOf (p, params::mode), p.editorView(), {}, { "Mirror", "Walk", "Orbit", "Explore" }),
          key (paramOf (p, params::key), paramOf (p, params::keyTonic), paramOf (p, params::keyMinor), paramOf (p, params::inKey), p.editorView(), m),
          theme (p.editorView(), {}, std::move (onTheme), 15.0f)
    {
        addAndMakeVisible (enable);
        addAndMakeVisible (mode);
        addAndMakeVisible (key);
        addAndMakeVisible (theme);
        enable.setTooltip ("Enable: off passes MIDI through");
        refresh();
    }

    void Header::refresh()
    {
        enable.refresh();
        mode.refresh();
        key.refresh();
        theme.setText (processor.editorView().dark ? utf8 ("\xe2\x98\x80") : utf8 ("\xe2\x98\xbe"));   // ☀ ☾
        if (model.sentence != sentence)
        {
            sentence = model.sentence;
            repaint (sentenceArea);
        }
        if (key.getWidth() < key.preferredWidth() - 1 || key.getWidth() > key.preferredWidth() + 24)
            resized();
    }

    void Header::resized()
    {
        auto r = getLocalBounds().reduced (14, 0);
        r.removeFromLeft (98);                                   // the name
        enable.setBounds (r.removeFromLeft (enable.preferredWidth() + 6));
        r.removeFromLeft (10);
        mode.setBounds (r.removeFromLeft (mode.preferredWidth() + 4));
        theme.setBounds (r.removeFromRight (24));
        r.removeFromLeft (12);
        key.setBounds (r.removeFromLeft (juce::jmin (r.getWidth() / 2, key.preferredWidth() + 12)));
        r.removeFromLeft (8);
        sentenceArea = r;
    }

    void Header::paint (juce::Graphics& g)
    {
        const auto& k = Ink::of (processor.editorView().dark);
        g.setColour (k.text);
        g.setFont (Ink::title (21.0f));
        g.drawText ("TonnetzML", getLocalBounds().reduced (14, 0).withWidth (100), juce::Justification::centredLeft, false);

        // The sentence: source in teal, the word in ink, the result in vermilion.
        g.setFont (Ink::note (14.5f));
        const auto arrow = utf8 (" \xe2\x94\x80\xe2\x94\x80");
        const int split = sentence.indexOf (arrow);
        auto area = sentenceArea.toFloat();
        if (sentence.isEmpty())
        {
            g.setColour (k.textFaint);
            g.setFont (Ink::ui (12.5f));
            g.drawText (utf8 ("play a chord, or click a triangle"), area, juce::Justification::centredRight, true);
            return;
        }
        const auto src = split >= 0 ? sentence.substring (0, split) : sentence;
        const auto rest = split >= 0 ? sentence.substring (split) : juce::String();
        const int head = rest.lastIndexOf (utf8 ("\xe2\x96\xba "));
        const auto mid = head >= 0 ? rest.substring (0, head + 2) : rest;
        const auto gen = head >= 0 ? rest.substring (head + 2) : juce::String();
        // Shrunk to fit (down to 11 points), then cut at the right.
        auto font = Ink::note (14.5f);
        float total = textWidth (font, src) + textWidth (font, mid) + textWidth (font, gen);
        if (total > area.getWidth())
        {
            font = Ink::note (juce::jmax (11.0f, 14.5f * area.getWidth() / total));
            total = textWidth (font, src) + textWidth (font, mid) + textWidth (font, gen);
        }
        g.setFont (font);
        float x = area.getRight() - juce::jmin (total, area.getWidth()) - 4.0f;
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (sentenceArea);
        const auto draw = [&] (const juce::String& s, juce::Colour c)
        {
            g.setColour (c);
            g.drawText (s, juce::Rectangle<float> (x, area.getY(), textWidth (font, s) + 60.0f, area.getHeight()), juce::Justification::centredLeft, false);
            x += textWidth (font, s);
        };
        draw (src, k.source);
        draw (mid, k.text);
        draw (gen, k.generated);
    }

    //==========================================================================
    Settings::Settings (TonnetzProcessor& p, std::function<void()> onLayoutChange)
        : processor (p), layoutChanged (std::move (onLayoutChange)),
          fold (p.editorView(), {}, [this]
          {
              processor.editorView().showSettings = ! processor.editorView().showSettings;
              refresh();
              if (layoutChanged)
                  layoutChanged();
          }, 11.5f)
    {
        const auto& v = p.editorView();
        auto add = [this] (HudControl* c) { owned.add (c); addAndMakeVisible (c); return c; };
        auto& P = p;
        lines.add ({ "out", { add (new HudCycle (paramOf (P, params::mix), v, {})) } });
        lines.add ({ "channels", { add (new HudCycle (paramOf (P, params::sourceChannel), v, "src")),
                                   add (new HudCycle (paramOf (P, params::genChannel), v, "gen")) } });
        lines.add ({ "voicing", { add (new HudCycle (paramOf (P, params::registerMode), v, {})),
                                  add (new HudCycle (paramOf (P, params::bass), v, "bass")),
                                  add (new HudCycle (paramOf (P, params::octave), v, "oct")) } });
        lines.add ({ "gather", { add (new HudCycle (paramOf (P, params::gatherMs), v, {}, " ms", 5)) } });
        lines.add ({ "orbit", { add (new HudCycle (paramOf (P, params::orbitCycle), v, {})),
                                add (new HudCycle (paramOf (P, params::orbitRate), v, utf8 ("\xe2\x99\xa9"))) } });
        lines.add ({ "walk", { add (new HudCycle (paramOf (P, params::walkInput), v, {})),
                               add (new HudToggle (paramOf (P, params::latch), v, {}, "latch")) } });
        lines.add ({ "op keys", { add (new HudToggle (paramOf (P, params::opZoneOn), v, {}, "on")),
                                  add (new HudCycle (paramOf (P, params::opZoneStart), v, "from", {}, 12)) } });
        lines.add ({ "held key", { add (new HudCycle (paramOf (P, params::keyTonic), v, {})),
                                   add (new HudToggle (paramOf (P, params::keyMinor), v, {}, "minor", "major")),
                                   add (new HudToggle (paramOf (P, params::inKey), v, {}, "in key")) } });
        lines.add ({ "test", { add (new HudToggle (paramOf (P, params::testRig), v, {}, "rig")) } });
        addAndMakeVisible (fold);
        refresh();
    }

    void Settings::controls (juce::Array<HudControl*>& out)
    {
        for (auto* c : owned)
            out.add (c);
    }

    juce::Rectangle<int> Settings::preferredSize() const
    {
        if (! processor.editorView().showSettings)
            return { 0, 0, 90, lineHeight };
        int w = 0;
        for (const auto& line : lines)
        {
            int lw = captionWidth;
            for (auto* c : line.items)
                lw += c->preferredWidth() + 10;
            w = juce::jmax (w, lw);
        }
        return { 0, 0, w + 16, lineHeight * (lines.size() + 1) + 8 };
    }

    void Settings::refresh()
    {
        const bool shown = processor.editorView().showSettings;
        fold.setText (shown ? utf8 ("settings \xe2\x96\xb4") : utf8 ("settings \xe2\x96\xbe"));
        bool widthChanged = false;
        for (const auto& line : lines)
            for (auto* c : line.items)
            {
                c->setVisible (shown);
                c->refresh();
                widthChanged = widthChanged || (shown && std::abs (c->getWidth() - c->preferredWidth()) > 0);
            }
        if (widthChanged && layoutChanged)
            layoutChanged();
    }

    void Settings::resized()
    {
        auto r = getLocalBounds().reduced (8, 4);
        fold.setBounds (r.removeFromTop (lineHeight).removeFromRight (fold.preferredWidth()));
        for (const auto& line : lines)
        {
            auto row = r.removeFromTop (lineHeight);
            for (int i = line.items.size(); --i >= 0;)
            {
                auto* c = line.items[i];
                c->setBounds (row.removeFromRight (c->preferredWidth()));
                row.removeFromRight (10);
            }
        }
        repaint();
    }

    void Settings::paint (juce::Graphics& g)
    {
        const auto& k = Ink::of (processor.editorView().dark);
        if (! processor.editorView().showSettings)
            return;
        // A soft wash behind the text so it reads over the lines (no border: not a panel).
        juce::ColourGradient wash (k.ground.withAlpha (0.0f), 0.0f, 0.0f, k.wash, 40.0f, 0.0f, false);
        g.setGradientFill (wash);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setFont (Ink::ui (11.5f));
        g.setColour (k.textFaint);
        auto r = getLocalBounds().reduced (8, 4);
        r.removeFromTop (lineHeight);
        for (const auto& line : lines)
        {
            auto row = r.removeFromTop (lineHeight);
            int left = row.getRight();
            for (auto* c : line.items)
                left = juce::jmin (left, c->getX());
            g.drawText (line.caption, juce::Rectangle<int> (left - captionWidth - 4, row.getY(), captionWidth, row.getHeight()), juce::Justification::centredRight, false);
        }
    }
}
